// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "radio_runtime.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>
#include <memory>

#include "core/channel_crypto.h"
#include "core/decoder_pool.h"
#include "core/frame_encoder.h"
#include "core/lifecycle.h"
#include "core/mic.h"
#include "core/mixer.h"
#include "core/net_node.h"
#include "core/opus_decoder.h"
#include "core/opus_encoder.h"
#include "core/peer_snapshot.h"
#include "core/playout.h"
#include "core/self_test.h"
#include "core/spsc_ring.h"
#include "core/supervisor.h"
#include "core/talk_key.h"
#include "core/tones.h"
#include "core/transmitter.h"
#include "es7210.h"
#include "es8311.h"
#include "reticulum_link.h"

namespace crewradio {
namespace esp {

namespace {

constexpr const char* kTag = "radio";

// The 4"'s audio pins (Waveshare BSP): I2S0 to the ES8311 (out) and from
// the ES7210 (in), the NS4150B's enable on GPIO53.
constexpr gpio_num_t kMclk = GPIO_NUM_13;
constexpr gpio_num_t kBclk = GPIO_NUM_12;
constexpr gpio_num_t kWs = GPIO_NUM_10;
constexpr gpio_num_t kDout = GPIO_NUM_9;
constexpr gpio_num_t kDin = GPIO_NUM_11;
constexpr gpio_num_t kAmpEnable = GPIO_NUM_53;

constexpr int kCore = 0;
constexpr UBaseType_t kAudioPriority = 17;
constexpr UBaseType_t kNetPriority = 16;
constexpr UBaseType_t kBeatPriority = 3;

enum Slot { kSlotNet = 0, kSlotAudio = 1 };

constexpr size_t kRingSlotBytes = sizeof(Delivery) + audio::kFrameBytes;
constexpr size_t kRingSlots = 32;  // 640 ms of one talker

int64_t mono_ms() { return esp_timer_get_time() / 1000; }

struct Shared {
  RadioConfig config;
  std::unique_ptr<NetNode> node;  // Lives through task restarts
  std::unique_ptr<SpscRing<kRingSlotBytes>> ring;
  std::unique_ptr<Mixer> mixer;
  std::unique_ptr<DecoderPool> decoders;
  std::unique_ptr<Playout> playout;
  std::unique_ptr<TaskPort> port;
  std::unique_ptr<Lifecycle> lifecycle;
  i2s_chan_handle_t tx = nullptr;
  i2s_chan_handle_t rx = nullptr;
  bool mic_ok = false;
  int32_t self_id = 0;
  PeerSnapshot peers;  // cr-net -> cr-audio
  std::unique_ptr<FrameEncoder> encoder;
  std::unique_ptr<Transmitter> transmitter;
  std::atomic<bool> touch_held{false};
  Supervisor supervisor;
  std::atomic<bool> clock_ok{false};
  std::atomic<uint8_t> shed{0};
  std::atomic<uint32_t> generation{0};
  std::atomic<bool> net_running{false};
  std::atomic<bool> audio_running{false};
  std::atomic<uint64_t> ring_drops{0};
  QueueHandle_t gnss = nullptr;  // int64_t epoch seconds, from the loop task
  SemaphoreHandle_t status_lock = nullptr;
  RadioStatus status;
};
Shared* g = nullptr;

void set_state(RadioState s) {
  if (g == nullptr) return;
  xSemaphoreTake(g->status_lock, portMAX_DELAY);
  g->status.state = s;
  xSemaphoreGive(g->status_lock);
}

// ---- cr-net -------------------------------------------------------------

struct Socket {
  int fd = -1;
  uint32_t ip = 0;         // Host order; 0 = no address yet
  uint32_t broadcast = 0;  // Host order

  void close_it() {
    if (fd >= 0) ::close(fd);
    fd = -1;
  }

  // The station interface's address and broadcast address; false with none.
  static bool address(uint32_t* ip, uint32_t* broadcast) {
    esp_netif_t* netif = esp_netif_get_default_netif();
    esp_netif_ip_info_t info;
    if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK ||
        info.ip.addr == 0) {
      return false;
    }
    *ip = ntohl(info.ip.addr);
    *broadcast = *ip | ~ntohl(info.netmask.addr);
    return true;
  }

  // (Re)opens when there is none or the address changed: the stack forgets
  // the group membership when the link goes down.
  bool ensure(uint32_t group, uint16_t port, int timeout_ms) {
    uint32_t now_ip = 0, now_bcast = 0;
    if (!address(&now_ip, &now_bcast)) {
      close_it();
      ip = 0;
      return false;
    }
    if (fd >= 0 && now_ip == ip) return true;
    close_it();
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    uint8_t ttl = 1, loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    // The task checks in between, and looks for what came over Reticulum.
    timeval tv = {0, timeout_ms * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in bind_to = {};
    bind_to.sin_family = AF_INET;
    bind_to.sin_port = htons(port);
    bind_to.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (sockaddr*)&bind_to, sizeof(bind_to)) != 0) {
      ESP_LOGE(kTag, "UDP port %u: bind failed (%d)", port, errno);
      close_it();
      return false;
    }
    ip_mreq m = {};
    m.imr_multiaddr.s_addr = htonl(group);
    m.imr_interface.s_addr = htonl(now_ip);
    if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m)) != 0) {
      // Unicast and broadcast still work; the group is only the first
      // contact.
      ESP_LOGW(kTag, "Joining the multicast group failed (%d)", errno);
    }
    ip = now_ip;
    broadcast = now_bcast;
    ESP_LOGI(kTag, "Listening on port %u, %s", port,
             inet_ntoa(*(in_addr*)&m.imr_interface.s_addr));
    return true;
  }

  void send_to(const uint8_t* p, size_t len, uint32_t addr, uint16_t port) {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(addr);
    // A failed send is transient (no buffer, no route yet): not reported.
    sendto(fd, p, len, 0, (sockaddr*)&to, sizeof(to));
  }
};

void publish(NetNode& node, const Socket& sock, int64_t now) {
  std::vector<Member> members;
  node.roster().snapshot(now, &members);
  const NetNode::Counters& c = node.counters();
  xSemaphoreTake(g->status_lock, portMAX_DELAY);
  g->status.members.swap(members);
  g->status.received = c.received;
  g->status.rejected = c.rejected;
  g->status.stale = c.stale;
  g->status.duplicates = c.duplicates;
  g->status.late = c.late;
  g->status.audio = c.audio;
  g->status.ring_drops = g->ring_drops.load();
  g->status.socket_open = sock.fd >= 0;
  g->status.relayed = c.relayed;
  g->status.reticulum = reticulum::status();
  xSemaphoreGive(g->status_lock);
}

// A packet for the WLAN: the unicasts LanPeers plans and, when it plans them,
// the group copies (a relayed talker's first frames, any hello), which the
// memory guard sheds for audio first.
void send_lan(NetNode& node, Socket& sock, const uint8_t* p, size_t len,
              bool hello, int64_t now) {
  const LanPeers::Plan plan = node.peers().plan(p, len, now);
  if (plan.group &&
      (hello || g->shed.load() < (uint8_t)Shed::kGroupCopies)) {
    sock.send_to(p, len, g->config.group, g->config.port);
    sock.send_to(p, len, sock.broadcast, g->config.port);
  }
  for (int i = 0; i < plan.count; i++) {
    sock.send_to(p, len, plan.addrs[i], g->config.port);
  }
}

// One packet from either transport through the pipeline: audio to cr-audio,
// a copy for the other transport where the bridge says so.
void take(NetNode& node, Socket& sock, const uint8_t* p, size_t len,
          NetNode::Via via, uint32_t from, int64_t now) {
  NetNode::Audio au;
  NetNode::Relay relay;
  if (node.on_packet(p, len, via, from, now, (int64_t)time(nullptr), &au,
                     &relay)) {
    uint8_t* slot = g->ring->write_slot();
    if (slot == nullptr || au.len > audio::kFrameBytes) {
      g->ring_drops++;
    } else {
      Delivery d{au.sender, (uint8_t)au.codec, 0,
                 (int16_t)(au.gap > 32767 ? 32767 : au.gap),
                 (uint16_t)au.len, 0};
      memcpy(slot, &d, sizeof(d));
      memcpy(slot + sizeof(d), au.data, au.len);
      g->ring->commit((uint16_t)(sizeof(d) + au.len));
    }
  }
  if (g->shed.load() >= (uint8_t)Shed::kTransmit) return;  // Nothing goes out
  if (relay.to_reticulum) reticulum::send(relay.data, relay.len);
  if (relay.to_lan && sock.fd >= 0) {
    send_lan(node, sock, relay.data, relay.len, relay.hello, now);
  }
}

void net_task(void* arg) {
  const uint32_t gen = (uint32_t)(uintptr_t)arg;
  NetNode& node = *g->node;
  Socket sock;
  static uint8_t buf[packet::kMaxSize + 1];  // One net task at a time
  static uint8_t from_rns[packet::kMaxSize];
  uint8_t hello[NetNode::kMaxHello];
  // With Reticulum on, the socket's wait is short: what came over Reticulum
  // waits at most that long in its ring.
  const int timeout_ms = g->config.reticulum ? 20 : 100;
  int64_t next_beat = 0;
  int64_t retry_at = 0;  // A socket that could not be opened: tried again
  g->supervisor.arm(kSlotNet, mono_ms());
  while (g->generation.load() == gen) {
    g->supervisor.checkin(kSlotNet, mono_ms());
    // The WLAN socket, opened again at most twice a second when it cannot be
    // (no address yet, a port in use). Without it the rest of the turn still
    // runs: what came over Reticulum, the hello there, the roster's expiry
    // and the status, so nobody stays on the screen after leaving.
    bool lan = false;
    if (sock.fd >= 0 || mono_ms() >= retry_at) {
      lan = sock.ensure(g->config.group, g->config.port, timeout_ms);
      if (!lan) retry_at = mono_ms() + 500;
    }
    const bool on_air =
        g->clock_ok.load() && g->shed.load() < (uint8_t)Shed::kPaused;
    if (lan) {
      sockaddr_in from = {};
      socklen_t from_len = sizeof(from);
      const int n = recvfrom(sock.fd, buf, sizeof(buf), 0, (sockaddr*)&from,
                             &from_len);
      if (n > 0 && on_air) {
        take(node, sock, buf, (size_t)n, NetNode::Via::kLan,
             ntohl(from.sin_addr.s_addr), mono_ms());
      } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        sock.close_it();  // Reopened on the next turn
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    }
    const int64_t now = mono_ms();
    // What came over Reticulum meanwhile, through the same pipeline.
    size_t m;
    while ((m = reticulum::receive(from_rns, sizeof(from_rns))) > 0) {
      if (on_air) take(node, sock, from_rns, m, NetNode::Via::kReticulum, 0, now);
    }
    if (sock.fd >= 0) {
      // Where this node's voice goes, for cr-audio.
      PeerView v;
      v.ip = sock.ip;
      v.broadcast = sock.broadcast;
      v.count = node.peers().live_addrs(now, v.addrs);
      g->peers.publish(v);
    } else {
      g->peers.publish(PeerView());  // No socket: no voice on the WLAN
    }
    if (now >= next_beat) {
      next_beat = now + 1000;
      // The hello: one a second, while the clock is trusted and the memory
      // guard lets us send. Hellos always keep their group copies.
      if (on_air && g->shed.load() < (uint8_t)Shed::kTransmit) {
        // What this node is on now: the WLAN while its socket is open, and
        // Reticulum while connected to its transport node (on Reticulum
        // alone it keeps the channel, as the plugin does).
        const uint8_t on =
            (uint8_t)((sock.fd >= 0 ? Hello::kLan : 0) |
                      (reticulum::connected() ? Hello::kReticulum : 0));
        node.set_transports(on);
        const size_t len =
            on != 0 ? node.build_hello((int64_t)time(nullptr), hello) : 0;
        if (len > 0) {
          if (sock.fd >= 0) send_lan(node, sock, hello, len, /*hello=*/true, now);
          reticulum::send(hello, len);
        }
      }
      node.roster().expire(now);
      publish(node, sock, now);
    }
  }
  sock.close_it();
  g->peers.publish(PeerView());  // No address: cr-audio sends nothing
  g->supervisor.disarm(kSlotNet);
  g->net_running.store(false);
  vTaskDelete(nullptr);
}

// ---- cr-audio -----------------------------------------------------------

// cr-audio's own socket for this node's voice (the receive socket belongs
// to cr-net; sharing its descriptor would race its reopen). Sends to the
// configured port, which is where every node listens.
struct TxSocket {
  int fd = -1;
  uint32_t ip = 0;

  void close_it() {
    if (fd >= 0) ::close(fd);
    fd = -1;
    ip = 0;
  }
  bool ensure(uint32_t station_ip) {
    if (station_ip == 0) {
      close_it();
      return false;
    }
    if (fd >= 0 && ip == station_ip) return true;
    close_it();
    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one));
    uint8_t ttl = 1, loop = 0;
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));
    in_addr ifaddr;
    ifaddr.s_addr = htonl(station_ip);
    setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ifaddr, sizeof(ifaddr));
    ip = station_ip;
    return true;
  }
  void send_to(const uint8_t* p, size_t len, uint32_t addr, uint16_t port) {
    sockaddr_in to = {};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(addr);
    sendto(fd, p, len, MSG_DONTWAIT, (sockaddr*)&to, sizeof(to));
  }
};

void cue(tones::Cue c) {
  const std::vector<int16_t> f = tones::frames(c);
  for (size_t i = 0; i + audio::kFrameSamples <= f.size();
       i += audio::kFrameSamples) {
    g->mixer->cue(&f[i]);
  }
}

void audio_task(void* arg) {
  const uint32_t gen = (uint32_t)(uintptr_t)arg;
  int16_t out[audio::kFrameSamples];
  int16_t stereo[2 * audio::kFrameSamples];
  int16_t mono[audio::kFrameSamples];
  uint8_t pkt[Transmitter::kMaxPacket];
  TalkKey key;
  BurstFloor floor;
  TxSocket tx;
  int timeouts = 0;
  int level = 0;
  int64_t next_sweep = 0;
  const RadioConfig& c = g->config;
  g->supervisor.arm(kSlotAudio, mono_ms());
  while (g->generation.load() == gen) {
    const int64_t now = mono_ms();
    g->supervisor.checkin(kSlotAudio, now);

    // What came in: decoded and queued in the mixer.
    uint16_t len = 0;
    const uint8_t* slot;
    while ((slot = g->ring->read_slot(&len)) != nullptr) {
      Delivery d;
      memcpy(&d, slot, sizeof(d));
      if (len >= sizeof(d) && d.len == len - sizeof(d)) {
        g->playout->deliver(d, slot + sizeof(d), now);
      }
      g->ring->release();
    }

    // The talk key, once a frame.
    const bool button = c.talk_gpio >= 0 &&
                        gpio_get_level((gpio_num_t)c.talk_gpio) == 0;
    const bool touch = c.touch_talk && g->touch_held.load();
    const bool may_talk = g->mic_ok && g->clock_ok.load() &&
                          g->shed.load() < (uint8_t)Shed::kTransmit;
    switch (key.sample(button && may_talk, touch && may_talk, now)) {
      case TalkKey::Event::kKeyed:
        g->encoder->reset();
        g->mixer->set_gain(0.0f);  // Half duplex: the crew is not played
        if (c.key_beeps) cue(tones::Cue::kKeyed);
        ESP_LOGI(kTag, "Talking");
        break;
      case TalkKey::Event::kUnkeyed:
        g->mixer->set_gain(1.0f);
        if (c.key_beeps) cue(tones::Cue::kUnkeyed);
        ESP_LOGI(kTag, "Listening");
        break;
      case TalkKey::Event::kTimedOut:
        g->mixer->set_gain(1.0f);
        timeouts++;
        cue(tones::Cue::kTimedOut);  // Always: the talker must know
        ESP_LOGW(kTag, "Talk time-out: the mic stays off until released");
        break;
      case TalkKey::Event::kNone:
        break;
    }

    // One frame out (blocks until the DMA has room: the clock of the loop)
    // and one frame in.
    g->mixer->tick(now, out);
    for (size_t i = 0; i < audio::kFrameSamples; i++) {
      stereo[2 * i] = stereo[2 * i + 1] = out[i];
    }
    size_t moved = 0;
    i2s_channel_write(g->tx, stereo, sizeof(stereo), &moved, pdMS_TO_TICKS(200));
    bool heard = false;
    if (g->rx != nullptr) {
      moved = 0;
      heard = i2s_channel_read(g->rx, stereo, sizeof(stereo), &moved,
                               pdMS_TO_TICKS(40)) == ESP_OK &&
              moved == sizeof(stereo);
    }
    if (heard) {
      mic::take_channel(stereo, c.mic_channel, mono);
      level = mic::rms(mono);
    }

    // While keyed: this frame to the crew.
    if (heard && key.talking()) {
      PeerView v;
      const size_t n = g->transmitter->build(g->encoder.get(), mono,
                                             (int64_t)time(nullptr), pkt);
      if (n > 0 && g->peers.read(&v) && tx.ensure(v.ip)) {
        // The group copies while no peer is known, and for the first frames
        // of each burst unless the memory guard has shed them.
        const bool floor_copy =
            g->shed.load() < (uint8_t)Shed::kGroupCopies &&
            floor.take(g->self_id, now);
        if (v.count == 0 || floor_copy) {
          tx.send_to(pkt, n, c.group, c.port);
          tx.send_to(pkt, n, v.broadcast, c.port);
        }
        for (int i = 0; i < v.count; i++) tx.send_to(pkt, n, v.addrs[i], c.port);
      }
      // And on every Reticulum link (nothing while there is none).
      if (n > 0) reticulum::send(pkt, n);
    }

    if (now >= next_sweep) {
      next_sweep = now + 1000;
      g->decoders->sweep(now);
      xSemaphoreTake(g->status_lock, portMAX_DELAY);
      g->status.frames = g->playout->counters().frames;
      g->status.decode_errors = g->playout->counters().decode_errors;
      g->status.concealed = g->mixer->concealed_frames();
      g->status.talking = key.talking();
      g->status.sent = g->transmitter->sent();
      g->status.timeouts = timeouts;
      g->status.mic_level = level;
      g->status.mic_ok = g->mic_ok;
      xSemaphoreGive(g->status_lock);
    }
  }
  tx.close_it();
  g->mixer->set_gain(1.0f);
  g->supervisor.disarm(kSlotAudio);
  g->audio_running.store(false);
  vTaskDelete(nullptr);
}

void beat_task(void*);

// The tasks, for core/lifecycle.h (which decides; this only does).
class EspTaskPort : public TaskPort {
 public:
  bool create(RadioTask task) override {
    const uint32_t gen = g->generation.load();
    switch (task) {
      case RadioTask::kNet:
        return spawn(net_task, "cr-net", 8192, gen, kNetPriority,
                     &g->net_running);
      case RadioTask::kAudio:
        return spawn(audio_task, "cr-audio", 12288, gen, kAudioPriority,
                     &g->audio_running);
      case RadioTask::kBeat:
        if (xTaskCreatePinnedToCore(beat_task, "cr-beat", 4096, nullptr,
                                    kBeatPriority, nullptr, kCore) == pdPASS) {
          return true;
        }
        ESP_LOGE(kTag, "The radio's supervisor could not be created");
        return false;
    }
    return false;
  }
  // Both workers see the new generation at their next turn and leave.
  void request_stop() override { g->generation++; }
  bool wait_stopped(int64_t timeout_ms) override {
    for (int64_t waited = 0;
         waited < timeout_ms && (g->net_running || g->audio_running);
         waited += 100) {
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    const bool stopped = !g->net_running && !g->audio_running;
    if (!stopped) {
      ESP_LOGE(kTag, "A radio task did not stop within %lld ms",
               (long long)timeout_ms);
    }
    return stopped;
  }
  void stopped_for_good() override {
    ESP_LOGE(kTag, "The radio stays off (the gauge carries on)");
    g->supervisor.disarm(kSlotNet);
    g->supervisor.disarm(kSlotAudio);
    set_state(RadioState::kStopped);
  }

 private:
  static bool spawn(TaskFunction_t fn, const char* name, uint32_t stack,
                    uint32_t gen, UBaseType_t priority,
                    std::atomic<bool>* running) {
    running->store(true);
    if (xTaskCreatePinnedToCore(fn, name, stack, (void*)(uintptr_t)gen,
                                priority, nullptr, kCore) == pdPASS) {
      return true;
    }
    running->store(false);
    ESP_LOGE(kTag, "Task %s could not be created", name);
    return false;
  }
};

// ---- cr-beat ------------------------------------------------------------

void start_sntp_once() {
  static bool started = false;
  if (started || esp_sntp_enabled()) {
    started = true;
    return;
  }
  esp_netif_t* netif = esp_netif_get_default_netif();
  esp_netif_ip_info_t info;
  if (netif == nullptr || esp_netif_get_ip_info(netif, &info) != ESP_OK ||
      info.ip.addr == 0) {
    return;  // Not on the network yet
  }
  // The boat's router first (many serve NTP, and a boat is often offline),
  // then the public pool.
  static char gateway[16];
  esp_ip4addr_ntoa(&info.gw, gateway, sizeof(gateway));
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, gateway);
  esp_sntp_setservername(1, "pool.ntp.org");
  esp_sntp_init();
  started = true;
  ESP_LOGI(kTag, "SNTP from %s, then pool.ntp.org", gateway);
}

void beat_task(void*) {
  ClockPolicy clock;
  MemoryGuard guard;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    const int64_t now = mono_ms();
    start_sntp_once();
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      clock.sntp_synced(now);
    }
    int64_t gnss_s;
    while (xQueueReceive(g->gnss, &gnss_s, 0) == pdTRUE) {
      if (clock.gnss(gnss_s, (int64_t)time(nullptr), now)) {
        timeval tv = {(time_t)gnss_s, 0};
        settimeofday(&tv, nullptr);
        ESP_LOGI(kTag, "Clock set from the boat's GNSS");
      }
    }
    const ClockSource source = clock.source(now, (int64_t)time(nullptr));
    g->clock_ok.store(source != ClockSource::kNone);
    const uint32_t free_dma =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    const Shed before = (Shed)g->shed.load();
    const Shed shed = guard.update(free_dma);
    g->shed.store((uint8_t)shed);
    if (shed != before) {
      ESP_LOGW(kTag, "Memory guard: %s (%u bytes of DMA RAM free)",
               shed_text(shed), (unsigned)free_dma);
    }

    if (!g->lifecycle->stopped()) {
      const int stalled = g->supervisor.stalled(now);
      if (stalled >= 0) {
        const Supervisor::Verdict v = g->supervisor.on_stall(now);
        ESP_LOGE(kTag, "Radio task %d stalled: %s", stalled,
                 v == Supervisor::Verdict::kRestart ? "restarting the radio"
                                                    : "giving up");
        g->lifecycle->on_stall(v);
      }
    }

    xSemaphoreTake(g->status_lock, portMAX_DELAY);
    g->status.clock = source;
    g->status.shed = shed;
    g->status.free_dma = free_dma;
    g->status.restarts = g->supervisor.restarts();
    g->status.state = running_state(!g->lifecycle->stopped(), shed, source);
    xSemaphoreGive(g->status_lock);
  }
}

// ---- audio --------------------------------------------------------------

bool start_audio(int volume, float mic_gain_db) {
  gpio_config_t amp = {};
  amp.pin_bit_mask = 1ULL << kAmpEnable;
  amp.mode = GPIO_MODE_OUTPUT;
  gpio_config(&amp);
  gpio_set_level(kAmpEnable, 0);  // Off until the codec is set up: no pop

  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.dma_desc_num = 3;
  chan.dma_frame_num = audio::kFrameSamples;  // One 20 ms frame a descriptor
  chan.auto_clear = true;                     // Silence, not a loop, if late
  // Full duplex on one controller: out to the ES8311, in from the ES7210,
  // one clock, the same stereo slots both ways (the ES7210 sends MIC1 left,
  // MIC2 right; the speaker frame is sent on both).
  if (i2s_new_channel(&chan, &g->tx, &g->rx) != ESP_OK) return false;
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(audio::kSampleRate);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // 4.096 MHz
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                     I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = kMclk;
  std.gpio_cfg.bclk = kBclk;
  std.gpio_cfg.ws = kWs;
  std.gpio_cfg.dout = kDout;
  std.gpio_cfg.din = kDin;
  if (i2s_channel_init_std_mode(g->tx, &std) != ESP_OK ||
      i2s_channel_init_std_mode(g->rx, &std) != ESP_OK ||
      i2s_channel_enable(g->tx) != ESP_OK ||
      i2s_channel_enable(g->rx) != ESP_OK) {
    return false;
  }
  // MCLK runs now: the codecs can lock to it.
  if (!es8311_start() || !es8311_set_volume(volume)) return false;
  // Without the mic the unit still listens.
  g->mic_ok = es7210_start(mic_gain_db);
  if (!g->mic_ok) ESP_LOGW(kTag, "No microphone: listening only");
  gpio_set_level(kAmpEnable, 1);
  return true;
}

// The channel's Reticulum destination and link key proofs come from the
// packet key, so only the crew can find this node or link with it.
void start_reticulum(const RadioConfig& config) {
  reticulum::Config rc;
  rc.host = config.reticulum_host;
  rc.port = config.reticulum_port;
  ChannelCrypto crypto(config.key);
  if (!crypto.reticulum_tag(rc.tag) ||
      !crypto.reticulum_confirm_key(rc.confirm_key)) {
    ESP_LOGE(kTag, "Reticulum: the channel's keys could not be derived");
    return;
  }
  // Connect only while the clock is trusted (a packet with a wrong time is
  // stale to everyone) and the memory guard leaves Reticulum on.
  rc.may_connect = []() {
    return g->clock_ok.load() && g->shed.load() < (uint8_t)Shed::kReticulum;
  };
  rc.stopped = []() { return g->lifecycle->stopped(); };
  reticulum::start(rc);
  memset(rc.confirm_key, 0, sizeof(rc.confirm_key));
}

}  // namespace

const char* state_text(RadioState s) {
  switch (s) {
    case RadioState::kOff: return "Off";
    case RadioState::kNoKey: return "No channel key";
    case RadioState::kSelfTestFailed: return "Self-test failed";
    case RadioState::kNoAudio: return "No audio (codec or I2S)";
    case RadioState::kNoClock: return "No clock";
    case RadioState::kOnChannel: return "On channel";
    case RadioState::kPaused: return "Paused: memory";
    case RadioState::kStopped: return "Stopped (radio tasks not running)";
  }
  return "?";
}

const char* clock_text(ClockSource s) {
  switch (s) {
    case ClockSource::kSntp: return "SNTP";
    case ClockSource::kGnss: return "GNSS (Signal K)";
    case ClockSource::kNone: break;
  }
  return "None";
}

const char* shed_text(Shed s) {
  switch (s) {
    case Shed::kNone: return "Nothing shed";
    case Shed::kReticulum: return "Reticulum off";
    case Shed::kGroupCopies: return "Audio unicast only";
    case Shed::kTransmit: return "Not transmitting";
    case Shed::kPaused: return "Paused";
  }
  return "?";
}

void begin(const RadioConfig& config) {
  if (g != nullptr) return;
  g = new Shared();
  g->config = config;
  g->status.channel = config.channel;
  g->status.volume = config.volume;
  g->status_lock = xSemaphoreCreateMutex();
  g->gnss = xQueueCreate(4, sizeof(int64_t));
  if (!config.enabled) {
    g->status.state = RadioState::kOff;
    return;
  }
  if (!config.has_key) {
    g->status.state = RadioState::kNoKey;
    ESP_LOGW(kTag, "Crew radio on, but no channel key saved");
    return;
  }
  if (!self_test::run()) {
    g->status.state = RadioState::kSelfTestFailed;
    ESP_LOGE(kTag, "Self-test failed: the radio stays off");
    return;
  }
  if (!start_audio(config.volume, config.mic_gain_db)) {
    g->status.state = RadioState::kNoAudio;
    ESP_LOGE(kTag, "Audio did not start: the radio stays off");
    return;
  }
  // A new sender id every boot: the phones remember each sender's last
  // sequence number for their whole run, so a node that came back with the
  // same id and numbers from 0 would be refused as a replay.
  int32_t self_id;
  esp_fill_random(&self_id, sizeof(self_id));
  NetNode::Config net;
  net.name = config.name;
  net.max_hops = config.max_hops;
  // The bridge only where there are two transports to bridge.
  net.relay = config.reticulum && config.relay;
  // The large tables (the seen-caches, ~0.7 MB; the mixer) are allocated
  // here, once, and go to PSRAM (Arduino-ESP32 puts allocations over 4 KB
  // there).
  g->node.reset(new NetNode(config.key, self_id, net));
  g->self_id = self_id;
  // This node's voice: the same sender id as its hellos.
  g->transmitter.reset(new Transmitter(config.key, self_id, config.max_hops));
  if (config.opus) {
    auto* opus = new OpusFrameEncoder();
    if (opus->ok()) {
      g->encoder.reset(opus);
    } else {
      delete opus;
      ESP_LOGW(kTag, "Opus encoder failed: sending PCM");
    }
  }
  if (!g->encoder) g->encoder.reset(new PcmFrameEncoder());
  if (config.talk_gpio >= 0) {
    gpio_config_t button = {};
    button.pin_bit_mask = 1ULL << config.talk_gpio;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;  // To ground when pressed
    gpio_config(&button);
  }
  g->ring.reset(new SpscRing<kRingSlotBytes>(kRingSlots));
  g->mixer.reset(new Mixer());
  std::vector<std::unique_ptr<FrameDecoder>> decoders;
  for (size_t i = 0; i < DecoderPool::kMax; i++) {
    decoders.emplace_back(new OpusFrameDecoder());
  }
  g->decoders.reset(new DecoderPool(std::move(decoders)));
  g->playout.reset(new Playout(g->mixer.get(), g->decoders.get()));
  g->status.state = RadioState::kNoClock;
  ESP_LOGI(kTag, "Crew radio as \"%s\", sender %08lx", config.name.c_str(),
           (unsigned long)(uint32_t)self_id);
  // The workers, then cr-beat; any of them failing leaves the radio
  // Stopped (core/lifecycle.h).
  g->port.reset(new EspTaskPort());
  g->lifecycle.reset(new Lifecycle(g->port.get()));
  if (!g->lifecycle->start()) return;
  if (config.reticulum) start_reticulum(config);
}

void set_touch_talk(bool held) {
  if (g != nullptr) g->touch_held.store(held);
}

void note_gnss_time(int64_t epoch_s) {
  if (g != nullptr && g->gnss != nullptr) xQueueSend(g->gnss, &epoch_s, 0);
}

RadioStatus status() {
  RadioStatus s;
  if (g == nullptr) return s;
  xSemaphoreTake(g->status_lock, portMAX_DELAY);
  s = g->status;
  xSemaphoreGive(g->status_lock);
  return s;
}

}  // namespace esp
}  // namespace crewradio
#endif
