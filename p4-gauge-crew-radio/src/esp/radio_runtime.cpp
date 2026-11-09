// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "log_level.h"  // First: INFO and WARN not compiled out
#include "radio_runtime.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>
#include <math.h>
#include <mbedtls/platform_util.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <atomic>
#include <memory>
#include <new>

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
#include "core/channel_floor.h"
#include "core/encode_budget.h"
#include "core/talk_key.h"
#include "core/tones.h"
#include "core/transmitter.h"
#include "core/voice.h"
#include "port/random.h"
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
// The ES7210's TDM slots (core/mic.h): MIC1, MIC3 = the echo reference (the
// ES8311's output, wired back on the board), MIC2, MIC4 (not connected), as
// Waveshare's firmware for this board reads them.
constexpr int kSlotMic1 = 0;
constexpr int kSlotEchoRef = 1;
constexpr int kSlotMic2 = 2;

constexpr int kCore = 0;
constexpr UBaseType_t kAudioPriority = 17;
// Passes in a row over a frame's time before cr-audio steps aside a tick.
constexpr int kOverrunPasses = 10;
constexpr UBaseType_t kNetPriority = 16;
constexpr UBaseType_t kBeatPriority = 3;
// A touch sample older than this is a release. The loop samples every 20 ms
// but an LVGL pass can take 150-175 ms (a compass page turning), so well
// clear of that, and far under the 5 s WiFi stall it is for.
constexpr int64_t kTouchFreshMs = 500;

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
  // When the loop task last sampled the touch (mono ms). The loop can sit in
  // a WiFi call to the C6 for up to 5 s; a hold it sampled before that is
  // not taken as still held, or the mic would stay open after the fingers
  // left the glass.
  std::atomic<int64_t> touch_at{0};
  // The talk key's state, written by cr-audio every frame (status() reads
  // it): the talker's own screen shows it within a frame, not at the next
  // once-a-second copy of the status.
  std::atomic<bool> talking{false};
  std::atomic<bool> busy{false};  // RadioStatus::busy
  // cr-audio's longest pass of work (its I2S waits left out) and its longest
  // Opus encode since cr-cpu last looked, in us: what core 0 has to spare.
  std::atomic<int> audio_work_max_us{0};
  std::atomic<int> encode_max_us{0};
  // How hard the encoder may work (core/encode_budget.h): kept with the
  // encoder, so a cr-audio the supervisor restarts goes on from where the
  // encoder is, not from kComplexity (its next step would raise it).
  EncodeBudget budget{OpusFrameEncoder::kComplexity};
  Supervisor supervisor;
  std::atomic<bool> clock_ok{false};
  std::atomic<uint8_t> shed{0};
  std::atomic<uint32_t> generation{0};
  std::atomic<bool> net_running{false};
  std::atomic<bool> audio_running{false};
  std::atomic<uint64_t> ring_drops{0};
  // The levels asked for (set_volume, set_mic_gain); cr-audio applies them.
  std::atomic<int> volume{70};
  std::atomic<int> mic_gain_tenths{300};  // Tenths of a dB
  // The mic's cleanup as asked for (set_voice); cr-audio applies it.
  std::atomic<bool> echo_cancel{true};
  std::atomic<int> noise_db{voice::kDefaultNoiseSuppressDb};
  std::atomic<bool> full_duplex{false};
  std::unique_ptr<voice::Processor> voice;  // cr-audio's alone
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
  g->status.rejected = c.rejected + c.flooded;  // Over its source budget too
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
          NetNode::Via via, uint32_t from, int64_t now,
          uint32_t link_tag = 0) {
  NetNode::Audio au;
  NetNode::Relay relay;
  if (node.on_packet(p, len, via, from, now, (int64_t)time(nullptr), &au,
                     &relay, link_tag)) {
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
  if (relay.to_reticulum) {
    // A frame that came over Reticulum may have just taught its link's far
    // end: that link's skip goes now, not at the next 250 ms refresh, so a
    // local talker's own copy never goes back to it.
    if (link_tag != 0) {
      const int slot = Forwarder::tag_slot(link_tag);
      reticulum::set_skip(slot, node.forwarder().skip_tag(slot, now));
    }
    reticulum::send(relay.data, relay.len, /*voice=*/!relay.hello);
  }
  if (relay.to_lan && sock.fd >= 0) {
    send_lan(node, sock, relay.data, relay.len, relay.hello, now);
  }
}

// cr-net: the WLAN socket and what came over Reticulum through the receive
// pipeline, the bridge, split horizon's skip list, the hello and the roster.
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
  int64_t next_skip = 0;
  int talkers = 0;       // As last published
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
        // Reopened, but not at once: an error that keeps coming back would
        // otherwise spin this task (priority 16) on core 0.
        sock.close_it();
        retry_at = mono_ms() + 500;
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(timeout_ms));
    }
    const int64_t now = mono_ms();
    // What came over Reticulum meanwhile, through the same pipeline.
    size_t m;
    uint32_t link_tag = 0;
    while ((m = reticulum::receive(from_rns, sizeof(from_rns), &link_tag)) >
           0) {
      if (on_air) {
        take(node, sock, from_rns, m, NetNode::Via::kReticulum, 0, now,
             link_tag);
      }
    }
    if (g->config.reticulum && now >= next_skip) {
      // Split horizon (core/forwarder.h): the links whose far end hears us
      // on the WLAN get none of what this node sends over Reticulum.
      next_skip = now + 250;
      for (int slot = 0; slot < Forwarder::kMaxLinks; slot++) {
        reticulum::set_skip(slot, node.forwarder().skip_tag(slot, now));
      }
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
        // alone it keeps the channel, as the plugin does). kRnsUp only with
        // a confirmed link to a node off this WLAN, and only when this node
        // bridges at all: the card's Relay on, a hop limit that lets a
        // packet go one hop further (relay_ttl gives 0 below 2), and the
        // WLAN up. A talker not yet linked ashore must not make the
        // forwarder leave its voice to it, which nobody ashore would then
        // hear, and a node that bridges nothing must not be elected to (the
        // app's Forwarding.bridges and the plugin's bridges() are the same
        // rule). kFwdAvoid always: with little RAM this node forwards the
        // boat's voice only when nobody else can (core/forwarder.h).
        const bool bridges =
            g->config.relay && g->config.max_hops >= 2 && sock.fd >= 0;
        const uint8_t on = (uint8_t)(
            (sock.fd >= 0 ? Hello::kLan : 0) |
            (reticulum::connected() ? Hello::kReticulum : 0) |
            (bridges && reticulum::linked_remote() ? Hello::kRnsUp : 0));
        node.set_transports(on != 0 ? (uint8_t)(on | Hello::kFwdAvoid) : 0);
        const size_t len =
            on != 0 ? node.build_hello((int64_t)time(nullptr), hello) : 0;
        if (len > 0) {
          if (sock.fd >= 0) send_lan(node, sock, hello, len, /*hello=*/true, now);
          reticulum::send(hello, len, /*voice=*/false);
        }
      }
      node.roster().expire(now);
      publish(node, sock, now);
      talkers = node.roster().talking_count(now);
    } else if (node.roster().talking_count(now) != talkers) {
      // Somebody started or stopped talking: the screen's halo shows it
      // now, not at the next once-a-second copy.
      publish(node, sock, now);
      talkers = node.roster().talking_count(now);
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

// The I2S frames of one cr-audio task: 32-bit stereo words both ways (the
// ES7210's four TDM slots in, the speaker's frame out), the slots unpacked,
// and MIC1/MIC2 as an interleaved pair. 5 KB, on the heap rather than the
// task's stack.
struct AudioBuffers {
  int32_t words[2 * audio::kFrameSamples];
  int16_t slots[mic::kTdmSlots][audio::kFrameSamples];
  int16_t pair[2 * audio::kFrameSamples];
};

// cr-audio: one 20 ms frame a pass, clocked by the I2S DMA. What came in is
// decoded into the mixer and played; the talk key is sampled (its state
// published to `talking` every frame); the mic is read, cleaned up and,
// while keyed, encoded and sent. Once a second it sweeps the decoders and
// copies its counters to the status. `arg` is the generation it runs for.
void audio_task(void* arg) {
  const uint32_t gen = (uint32_t)(uintptr_t)arg;
  int16_t out[audio::kFrameSamples];
  int16_t mono[audio::kFrameSamples];
  std::unique_ptr<AudioBuffers> buf(new (std::nothrow) AudioBuffers());
  if (!buf) {
    // As a task that stopped: the supervisor sees no check-ins and tries
    // again, and leaves the radio off after three tries.
    ESP_LOGE(kTag, "No memory for the audio buffers");
    g->audio_running.store(false);
    vTaskDelete(nullptr);
    return;
  }
  int32_t* words = buf->words;
  int16_t* stereo = buf->pair;  // MIC1 left, MIC2 right
  const size_t word_bytes = sizeof(buf->words);
  uint8_t pkt[Transmitter::kMaxPacket];
  TalkKey key;
  BurstFloor floor;
  ChannelFloor channel(g->self_id);
  int overruns = 0;      // Passes in a row with more work than a frame
  int64_t gave_ms = 0;   // When the last "gave core 0 a moment" was logged
  TxSocket tx;
  int timeouts = 0;
  int level = 0;
  int64_t next_sweep = 0;
  // What this task last wrote to the codecs: none yet, so the first frame
  // writes the levels asked for (start_audio wrote the card's, which may
  // have changed while a stalled task was being restarted).
  int applied_volume = -1;
  int applied_gain = -1;
  int voice_us = 0;  // The longest cleanup of a frame since the last sweep
  int64_t voice_sum_us = 0;
  int voice_frames = 0;
  const int64_t started = mono_ms();
  bool mic_logged = false;
  const RadioConfig& c = g->config;
  // The mic's DMA ran from begin() with nobody reading it, so its queue is
  // full: emptied first, or the loop (one frame in per frame out) would keep
  // it full, and a turn even slightly late would lose a mic frame with no
  // sequence gap for the crew to conceal.
  if (g->rx != nullptr) {
    size_t moved = 0;
    while (i2s_channel_read(g->rx, words, word_bytes, &moved, 0) ==
               ESP_OK &&
           moved > 0) {
      moved = 0;
    }
  }
  g->supervisor.arm(kSlotAudio, mono_ms());
  while (g->generation.load() == gen) {
    const int64_t now = mono_ms();
    const int64_t pass_us = esp_timer_get_time();
    int64_t waited_us = 0;  // In the I2S calls, which are the loop's clock
    g->supervisor.checkin(kSlotAudio, now);

    // The levels, when the screen or the card changed them: a few I2C
    // bytes, here because this task owns the codecs. Taken as written
    // whether or not the codec answered: a failing bus is not retried every
    // frame at 50 ms a try.
    const int want_volume = g->volume.load();
    if (want_volume != applied_volume) {
      if (!es8311_set_volume(want_volume)) {
        ESP_LOGW(kTag, "Speaker volume %d not taken by the ES8311", want_volume);
      }
      applied_volume = want_volume;
    }
    const int want_gain = g->mic_gain_tenths.load();
    if (g->mic_ok && want_gain != applied_gain) {
      if (!es7210_set_gain(want_gain / 10.0f)) {
        ESP_LOGW(kTag, "Mic gain %.1f dB not taken by the ES7210",
                 want_gain / 10.0f);
      }
      applied_gain = want_gain;
    }
    voice::Processor* const vp = g->voice.get();
    if (vp != nullptr) {
      const bool want_echo = g->echo_cancel.load();
      const int want_noise = g->noise_db.load();
      if (want_echo != vp->echo_cancel() || want_noise != vp->noise_db()) {
        vp->set(want_echo, want_noise);
        ESP_LOGI(kTag, "Mic cleanup: echo cancelling %s, noise suppression %d dB",
                 want_echo ? "on" : "off", vp->noise_db());
      }
    }

    // What came in: decoded and queued in the mixer.
    uint16_t len = 0;
    const uint8_t* slot;
    while ((slot = g->ring->read_slot(&len)) != nullptr) {
      Delivery d;
      memcpy(&d, slot, sizeof(d));
      if (len >= sizeof(d) && d.len == len - sizeof(d)) {
        g->playout->deliver(d, slot + sizeof(d), now);
        channel.heard(d.sender, now);  // Another talker: channel busy
      }
      g->ring->release();
    }

    // The talk key, once a frame.
    const bool button = c.talk_gpio >= 0 &&
                        gpio_get_level((gpio_num_t)c.talk_gpio) == 0;
    const bool touch = c.touch_talk && g->touch_held.load() &&
                       now - g->touch_at.load() < kTouchFreshMs;
    const bool may_talk = g->mic_ok && g->clock_ok.load() &&
                          g->shed.load() < (uint8_t)Shed::kTransmit;
    // Channel busy (core/channel_floor.h): pressed while another talker is
    // heard, the key keys only when held through the busy cue; two keyed at
    // once, the higher id gives way. Half duplex only, as on the phones: full
    // duplex is an open intercom that mixes talkers on purpose (the floor
    // then sees nothing held, and stays idle).
    const bool listen = !g->full_duplex.load();
    bool floor_ok = true;
    int32_t holder = 0;
    switch (channel.sample(listen && (button || touch), key.talking(), now,
                           &floor_ok)) {
      case ChannelFloor::Event::kRefused:
        channel.busy(now, &holder);
        cue(tones::Cue::kBusy);  // Always: the talker must know
        ESP_LOGI(kTag, "Channel busy (%08x talking): held, not keyed",
                 (unsigned)holder);
        break;
      case ChannelFloor::Event::kOverride:
        ESP_LOGW(kTag, "Talking over the channel (held through the busy cue)");
        break;
      case ChannelFloor::Event::kYielded:
        channel.busy(now, &holder);
        cue(tones::Cue::kBusy);
        ESP_LOGI(kTag, "Gave way to %08x: the mic stays off until released",
                 (unsigned)holder);
        break;
      case ChannelFloor::Event::kNone:
        break;
    }
    if (!listen) floor_ok = true;
    g->busy.store(channel.holding_off());
    switch (key.sample(button, touch, may_talk && floor_ok, now)) {
      case TalkKey::Event::kKeyed:
        g->encoder->reset();
        // Half duplex unless asked: the crew is not played while talking.
        if (!g->full_duplex.load()) g->mixer->set_gain(0.0f);
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
    g->talking.store(key.talking());

    // One frame out (blocks until the DMA has room: the clock of the loop)
    // and one frame in.
    g->mixer->tick(now, out);
    // The speaker's 16-bit sample in the upper half of each 32-bit slot
    // (the ES8311 takes the first 16 bits of a slot), both channels.
    for (size_t i = 0; i < audio::kFrameSamples; i++) {
      words[2 * i] = words[2 * i + 1] = (int32_t)((uint32_t)(uint16_t)out[i] << 16);
    }
    size_t moved = 0;
    const int64_t io_us = esp_timer_get_time();
    i2s_channel_write(g->tx, words, word_bytes, &moved, pdMS_TO_TICKS(200));
    bool heard = false;
    if (g->rx != nullptr) {
      moved = 0;
      heard = i2s_channel_read(g->rx, words, word_bytes, &moved,
                               pdMS_TO_TICKS(40)) == ESP_OK &&
              moved == word_bytes;
    }
    waited_us += esp_timer_get_time() - io_us;
    if (heard) {
      mic::unpack_tdm(words, buf->slots);
      for (size_t i = 0; i < audio::kFrameSamples; i++) {
        stereo[2 * i] = buf->slots[kSlotMic1][i];
        stereo[2 * i + 1] = buf->slots[kSlotMic2][i];
      }
      mic::take_channel(stereo, c.mic_channel, mono);
      // Every frame, talking or not: the canceller keeps learning the
      // speaker's path while the crew is played.
      if (vp != nullptr) {
        const int64_t t0 = esp_timer_get_time();
        vp->process(mono, buf->slots[kSlotEchoRef], mono);
        const int us = (int)(esp_timer_get_time() - t0);
        if (us > voice_us) voice_us = us;
        voice_sum_us += us;
        voice_frames++;
      }
      level = mic::rms(mono);
    }

    // While keyed: this frame to the crew.
    // (Not after giving way: off at once, not after the key's release.)
    if (heard && key.talking() && floor_ok) {
      PeerView v;
      const int64_t enc_us = esp_timer_get_time();
      const size_t n = g->transmitter->build(g->encoder.get(), mono,
                                             (int64_t)time(nullptr), pkt);
      const int took_us = (int)(esp_timer_get_time() - enc_us);
      if (took_us > g->encode_max_us.load()) g->encode_max_us.store(took_us);
      // Too slow for the frame (the encoder's state is in PSRAM, behind the
      // cache the display shares): a lighter encode from the next frame.
      if (g->budget.took(took_us)) {
        g->encoder->set_complexity(g->budget.complexity());
        ESP_LOGW(kTag, "Opus encodes took up to %d us: complexity %d now",
                 took_us, g->budget.complexity());
      }
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
      // Not while the memory guard has shed Reticulum: cr-rns has gone or is
      // going, and the ring would only fill.
      if (n > 0 && g->shed.load() < (uint8_t)Shed::kReticulum) {
        reticulum::send(pkt, n, /*voice=*/true);
      }
    }

    if (now >= next_sweep) {
      next_sweep = now + 1000;
      g->decoders->sweep(now);
      // Both mic channels of this frame, as read (stereo holds the mic's
      // frame only when one was heard).
      mic::ChannelStats stats[2];
      uint16_t raw[8];
      int slot_rms[mic::kTdmSlots] = {};
      if (heard) {
        stats[0] = mic::channel_stats(stereo, 0);
        stats[1] = mic::channel_stats(stereo, 1);
        for (int s = 0; s < mic::kTdmSlots; s++) {
          slot_rms[s] = mic::rms(buf->slots[s]);
          raw[2 * s] = (uint16_t)buf->slots[s][0];
          raw[2 * s + 1] = (uint16_t)buf->slots[s][1];
        }
        if (!mic_logged && now - started >= 2000) {
          mic_logged = true;  // Once, the ADC settled: the boot log's view
          ESP_LOGI(kTag,
                   "Mic frame: MIC1 dc %d rms %d peak %d, MIC2 dc %d rms %d "
                   "peak %d, ref rms %d; cleanup %d us; stack %u bytes spare",
                   stats[0].dc, stats[0].ac_rms, stats[0].peak, stats[1].dc,
                   stats[1].ac_rms, stats[1].peak, slot_rms[kSlotEchoRef],
                   voice_us, (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        }
      }
      int echo_db = -1;
      if (vp != nullptr && !vp->take_echo_db(&echo_db)) echo_db = -1;
      xSemaphoreTake(g->status_lock, portMAX_DELAY);
      if (heard) {
        g->status.mic_stats[0] = stats[0];
        g->status.mic_stats[1] = stats[1];
        memcpy(g->status.mic_raw, raw, sizeof(raw));
        memcpy(g->status.mic_slot_rms, slot_rms, sizeof(slot_rms));
      }
      g->status.frames = g->playout->counters().frames;
      g->status.decode_errors = g->playout->counters().decode_errors;
      g->status.concealed = g->mixer->concealed_frames();
      g->status.sent = g->transmitter->sent();
      g->status.timeouts = timeouts;
      g->status.mic_level = level;
      g->status.mic_ok = g->mic_ok;
      g->status.echo_db = echo_db;
      g->status.voice_us = voice_us;
      g->status.voice_avg_us =
          voice_frames > 0 ? (int)(voice_sum_us / voice_frames) : 0;
      xSemaphoreGive(g->status_lock);
      voice_us = 0;
      voice_sum_us = 0;
      voice_frames = 0;
    }
    const int work_us =
        (int)(esp_timer_get_time() - pass_us - waited_us);
    if (work_us > g->audio_work_max_us.load()) {
      g->audio_work_max_us.store(work_us);
    }
    // The safety valve: more work than a frame, pass after pass, and the I2S
    // calls never wait, so this task (priority 17) would hold core 0 until
    // the task watchdog restarts the unit. A tick off then: the audio may
    // stutter, the unit stays up (the radio is a guest).
    overruns = work_us >= (int)audio::kFrameMs * 1000 ? overruns + 1 : 0;
    if (overruns >= kOverrunPasses) {
      overruns = 0;
      if (now - gave_ms >= 60000) {
        gave_ms = now;
        ESP_LOGW(kTag, "cr-audio behind (%d us of work a frame): giving core "
                       "0 a moment",
                 work_us);
      }
      vTaskDelay(1);
    }
  }
  tx.close_it();
  g->mixer->set_gain(1.0f);
  g->talking.store(false);
  g->busy.store(false);
  g->supervisor.disarm(kSlotAudio);
  g->audio_running.store(false);
  vTaskDelete(nullptr);
}

void beat_task(void*);
void cpu_task(void*);

// The tasks, for core/lifecycle.h (which decides; this only does).
class EspTaskPort : public TaskPort {
 public:
  // Creates one of the radio's tasks (cr-net, cr-audio, cr-beat) for the
  // current generation.
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
          // The core 0 watch, on core 1 so a busy core 0 cannot silence it.
          xTaskCreatePinnedToCore(cpu_task, "cr-cpu", 4096, nullptr, 4,
                                  nullptr, 1);
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

// cr-cpu: once a second, how much of the second core 0's idle task had (its
// run-time counter, in us; one task's, read in a moment). Logged only when
// it got under kIdleFloor: before the task watchdog (5 s without idle)
// would restart the unit, the line says how close it is, with cr-audio's
// longest pass of work and Opus encode in that second. (A walk of every
// task's state each second, its first form, held the kernel long enough to
// make the 4"'s screen flicker now and then: 2026-11-08.)
void cpu_task(void*) {
  constexpr int kIdleFloor = 15;  // Percent
  TaskHandle_t idle = xTaskGetIdleTaskHandleForCore(0);
  uint32_t idle_before = 0;
  int64_t at_before = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    TaskStatus_t st;
    vTaskGetInfo(idle, &st, pdFALSE, eInvalid);
    const int64_t at = esp_timer_get_time();
    const uint32_t idle_us = st.ulRunTimeCounter - idle_before;  // Wraps safely
    const int64_t window = at - at_before;
    const bool first = at_before == 0;
    idle_before = st.ulRunTimeCounter;
    at_before = at;
    const int audio_us = g->audio_work_max_us.exchange(0);
    const int enc_us = g->encode_max_us.exchange(0);
    if (first || window <= 0) continue;
    const int idle_pct = (int)((int64_t)idle_us * 100 / window);
    if (idle_pct >= kIdleFloor) continue;
    ESP_LOGW(kTag,
             "core 0 idle %d%%; cr-audio work max %d us/frame, encode max "
             "%d us",
             idle_pct, audio_us, enc_us);
  }
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
    // Connected: the WLAN socket open or Reticulum up, as cr-net last
    // published them.
    const bool connected =
        g->status.socket_open ||
        g->status.reticulum.state == ReticulumStatus::State::kConnected;
    g->status.state =
        running_state(!g->lifecycle->stopped(), shed, source, connected);
    g->status.beat_ms = mono_ms();
    xSemaphoreGive(g->status_lock);
  }
}

// ---- audio --------------------------------------------------------------

// Undoes what start_audio() set up when a later step of it fails: the I2S
// DMA is not left running into nothing, and the amplifier stays off.
void stop_audio() {
  gpio_set_level(kAmpEnable, 0);
  if (g->tx != nullptr) {
    i2s_channel_disable(g->tx);  // An error when never enabled: harmless
    i2s_del_channel(g->tx);
    g->tx = nullptr;
  }
  if (g->rx != nullptr) {
    i2s_channel_disable(g->rx);
    i2s_del_channel(g->rx);
    g->rx = nullptr;
  }
}

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
  // one clock, the same 32-bit stereo slots both ways (the ES7210 sends its
  // four TDM slots in them, core/mic.h unpack_tdm; the speaker frame is sent
  // on both).
  if (i2s_new_channel(&chan, &g->tx, &g->rx) != ESP_OK) return false;
  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(audio::kSampleRate);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // 4.096 MHz
  // 32-bit slots: a 64 fs bit clock, as the ES7210 needs for its four TDM
  // slots (Waveshare's firmware: 4 x 16 bits a frame). At 32 fs it ran, but
  // its inputs read about 1 LSB whatever the gain. The ES8311 sees standard
  // I2S with 32-bit slots and takes the first 16 bits of each.
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,
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
    stop_audio();
    return false;
  }
  // MCLK runs now: the codecs can lock to it.
  if (!es8311_start() || !es8311_set_volume(volume)) {
    stop_audio();
    return false;
  }
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
  mbedtls_platform_zeroize(rc.confirm_key, sizeof(rc.confirm_key));
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
    case RadioState::kNoNetwork: return "No network";
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
  set_volume(config.volume);
  set_mic_gain(config.mic_gain_db);
  set_voice(config.echo_cancel, config.noise_suppress_db, config.full_duplex);
  g->status_lock = xSemaphoreCreateMutex();
  g->gnss = xQueueCreate(4, sizeof(int64_t));
  if (g->status_lock == nullptr || g->gnss == nullptr) {
    // No tasks start, so status() reads it without the lock (which it
    // checks for): a null handle would assert, and take the gauge down.
    g->status.state = RadioState::kStopped;
    ESP_LOGE(kTag, "No memory for the radio's lock or queue: it stays off");
    return;
  }
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
  // For the screen's mic control from the start, not only after cr-audio's
  // first status write a second from now.
  g->status.mic_ok = g->mic_ok;
  if (g->mic_ok) {
    // speexdsp's states (~0.1 MB, to PSRAM); without them the mic goes out
    // as heard.
    g->voice.reset(new (std::nothrow) voice::Processor());
    if (g->voice && !g->voice->ok()) g->voice.reset();
    if (!g->voice) ESP_LOGW(kTag, "No memory for the echo canceller: mic as heard");
    g->status.voice_ok = g->voice != nullptr;
  }
  // A new sender id every boot: the phones remember each sender's last
  // sequence number for their whole run, so a node that came back with the
  // same id and numbers from 0 would be refused as a replay.
  // The first draw also seeds the DRBG (port/random_esp.cpp), here at boot
  // rather than mid-session.
  int32_t self_id;
  port::random_bytes((uint8_t*)&self_id, sizeof(self_id));
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
  // As fresh as cr-beat's first write a second from now: the screen would
  // otherwise read 0 as a stalled radio and show it stopped at boot.
  g->status.beat_ms = mono_ms();
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
  if (g != nullptr) {
    g->touch_at.store(mono_ms());
    g->touch_held.store(held);
  }
}

void note_gnss_time(int64_t epoch_s) {
  if (g != nullptr && g->gnss != nullptr) xQueueSend(g->gnss, &epoch_s, 0);
}

void set_volume(int volume) {
  if (g == nullptr) return;
  g->volume.store(volume < 0 ? 0 : volume > 100 ? 100 : volume);
}

void set_mic_gain(float db) {
  if (g == nullptr) return;
  if (!(db >= 0.0f)) db = 0.0f;  // NaN too
  if (db > mic::kMaxGainDb) db = mic::kMaxGainDb;
  g->mic_gain_tenths.store((int)lroundf(db * 10.0f));
}

void set_voice(bool echo_cancel, int noise_suppress_db, bool full_duplex) {
  if (g == nullptr) return;
  g->echo_cancel.store(echo_cancel);
  g->noise_db.store(noise_suppress_db < 0 ? 0
                    : noise_suppress_db > voice::kMaxNoiseSuppressDb
                        ? voice::kMaxNoiseSuppressDb
                        : noise_suppress_db);
  g->full_duplex.store(full_duplex);
}

// A copy of the radio's status for the screen and the card, from any task:
// the tasks' once-a-second copy, taken under its lock, with what is kept
// apart from it (the talk key's state, the levels and the mic's cleanup)
// read as it is now.
RadioStatus status() {
  RadioStatus s;
  if (g == nullptr) return s;
  if (g->status_lock == nullptr) {
    s = g->status;  // begin() stopped early
  } else {
    xSemaphoreTake(g->status_lock, portMAX_DELAY);
    s = g->status;
    xSemaphoreGive(g->status_lock);
  }
  s.talking = g->talking.load();
  s.busy = g->busy.load();
  s.volume = g->volume.load();
  s.mic_gain_db = g->mic_gain_tenths.load() / 10.0f;
  s.mic_channel = g->config.mic_channel;
  s.echo_cancel = g->echo_cancel.load();
  s.noise_suppress_db = g->noise_db.load();
  s.full_duplex = g->full_duplex.load();
  return s;
}

}  // namespace esp
}  // namespace crewradio
#endif
