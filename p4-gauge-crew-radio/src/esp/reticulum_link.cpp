// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "log_level.h"  // First: INFO and WARN not compiled out
#include "reticulum_link.h"

#include <errno.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <fcntl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>
#include <string.h>

#include <atomic>

#include "core/packet.h"
#include "core/rns/out_buffer.h"
#include "core/rns/reticulum_node.h"
#include "core/rns/rns_packet.h"

namespace crewradio {
namespace esp {
namespace reticulum {

namespace {

constexpr const char* kTag = "radio-rns";
constexpr size_t kRingBytes = 16 * 1024;  // Each way: ~100 Opus frames
constexpr uint32_t kStack = 12288;
// Below cr-beat (3): a flood of announces and link requests (20 signature
// checks a second, tens of ms each) must not delay the memory guard and the
// stall supervisor, which are what shed it.
constexpr UBaseType_t kPriority = 2;
constexpr BaseType_t kCore = 0;
constexpr int64_t kConnectTimeoutMs = 10000;
constexpr int64_t kStableMs = 30000;
constexpr int64_t kTickMs = 1000;
constexpr int64_t kBackoffMinMs = 1000;
constexpr int64_t kBackoffMaxMs = 15000;
// crew-radio's "radio under load" rules (2026-11-08). cr-rns never waits on
// the socket: what it writes goes into a buffer of its own
// (core/rns/out_buffer.h, 16 KB) and out as the socket takes it. Voice older
// than kVoiceMaxAgeMs when its turn comes, or arriving while more than
// kBacklogBytes wait, is dropped rather than sent late, and so is voice that
// waits that long in the buffer itself (a backlog of voice is no use to
// anyone, a receiver would still play it seconds late, and a node that
// stopped reading would stall the hub for every member). Only kNoProgressMs
// with bytes waiting and none taken closes the connection. At most
// kDrainBudget packets a pass, so the socket is read and the memory guard
// checked between them whatever the other tasks hand over.
constexpr size_t kBacklogBytes = 4 * 1024;
constexpr int64_t kVoiceMaxAgeMs = 200;
constexpr int64_t kNoProgressMs = 10000;
constexpr int kDrainBudget = 8;
// What a ring item carries ahead of its packet: out, when it was handed over
// and whether it is voice; in, the link it came on.
struct OutHead {
  int64_t at_ms;
  uint8_t voice;
  uint8_t pad[7];
};
static_assert(sizeof(OutHead) == 16, "8-byte aligned ring items");
constexpr size_t kInHead = sizeof(uint32_t);

int64_t mono_ms() { return esp_timer_get_time() / 1000; }

struct Shared {
  Config config;
  RingbufHandle_t to_rns = nullptr;
  RingbufHandle_t from_rns = nullptr;
  std::atomic<bool> connected{false};
  std::atomic<bool> linked{false};
  // A confirmed link to a node not on our own WLAN (split horizon's view):
  // what the hello's kRnsUp claims.
  std::atomic<bool> linked_remote{false};
  std::atomic<uint32_t> dropped_in{0};
  std::atomic<uint32_t> dropped_out{0};
  std::atomic<uint32_t> dropped_late{0};  // Voice too old, or a backlog
  // Split horizon: per link slot, the tag of a link not to send on (its far
  // end is on our WLAN), 0 for none. Written by cr-net, read by cr-rns.
  std::atomic<uint32_t> skip[rns::ReticulumNode::kMaxLinks] = {};
  SemaphoreHandle_t lock = nullptr;
  ReticulumStatus status;
};
// cr-rns's own, set before the task is created.
Shared* g = nullptr;
// The other tasks' view: published only once the whole of it exists (its
// lock first of all), so no reader ever takes a lock that is not there.
std::atomic<Shared*> g_ready{nullptr};
Shared* ready() { return g_ready.load(std::memory_order_acquire); }

void set_state(ReticulumStatus::State state, const std::string& note) {
  xSemaphoreTake(g->lock, portMAX_DELAY);
  g->status.state = state;
  g->status.note = note;  // Each state says why afresh, or nothing
  xSemaphoreGive(g->lock);
}

void publish(const rns::ReticulumNode& node, uint32_t reconnects) {
  const rns::ReticulumNode::Counters& c = node.counters();
  xSemaphoreTake(g->lock, portMAX_DELAY);
  g->status.links = node.link_count();
  g->status.peers = node.peer_count();
  g->status.reconnects = reconnects;
  g->status.tx = c.tx;
  g->status.rx = c.rx;
  g->status.announces = c.announces;
  g->status.takeovers = c.takeovers;
  g->status.unproven = c.unproven;
  g->status.dropped_in = g->dropped_in.load();
  g->status.dropped_out = g->dropped_out.load();
  g->status.dropped_late = g->dropped_late.load();
  xSemaphoreGive(g->lock);
}

// Frames each Reticulum packet into a buffer of its own, which goes out as
// the socket takes it (flush(), never waiting): cr-rns keeps reading its
// links whatever the uplink does. A packet that does not fit is dropped and
// counted; a buffer that has had bytes waiting and none taken for
// kNoProgressMs, or a socket error, breaks the connection, which is then
// opened again. (Before 2026-11-08 a blocking send held cr-rns for up to 2 s
// a frame: two talkers kept it there, its socket unread, the hub stalled for
// everyone and the unit's core 0 without a moment of idle: the task
// watchdog restarted it.)
class SocketWriter : public rns::ReticulumNode::Writer {
 public:
  int fd = -1;
  bool broken = false;
  uint32_t overflow = 0;

  void reset(int new_fd, int64_t now) {
    fd = new_fd;
    broken = false;
    out_.clear();
    progress_ms_ = now;
  }
  // What the packets written next are: voice handed over at at_ms (the
  // drain loop, around node->send for a voice item), or link control and
  // hellos (everything else, the default).
  void set_voice(bool voice, int64_t at_ms) {
    voice_ = voice;
    at_ms_ = at_ms;
  }
  // Frames one Reticulum packet into the output buffer, as voice or control
  // (set_voice).
  void write(const uint8_t* raw, size_t len) override {
    if (fd < 0 || broken) return;
    const size_t n = rns::frame(raw, len, frame_);
    if (!out_.append(frame_, n, voice_, at_ms_)) overflow++;
  }
  size_t pending() const { return out_.size(); }
  // Voice that waited past kVoiceMaxAgeMs dropped (out_buffer.h), then out
  // as much as the socket takes now. Returns the voice frames dropped.
  int flush(int64_t now) {
    if (fd < 0 || broken) return 0;
    const int dropped = out_.expire(now, kVoiceMaxAgeMs);
    if (out_.size() == 0) {
      progress_ms_ = now;
      return dropped;
    }
    const int w = ::send(fd, out_.data(), out_.size(), MSG_DONTWAIT);
    if (w > 0) {
      out_.consume((size_t)w);
      progress_ms_ = now;
    } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      broken = true;
    } else if (now - progress_ms_ >= kNoProgressMs) {
      broken = true;  // Bytes waiting and none taken
    }
    return dropped;
  }

 private:
  rns::OutBuffer out_;
  uint8_t frame_[rns::kMaxFrame];
  bool voice_ = false;
  int64_t at_ms_ = 0;
  int64_t progress_ms_ = 0;
};

// The links cr-net says not to send on (split horizon).
class SkipView : public rns::ReticulumNode::Skip {
 public:
  // True when cr-net has marked this link's slot and epoch as one not to send
  // on.
  bool skip(uint32_t tag) const override {
    const int slot = (int)(tag >> 24);
    return slot < rns::ReticulumNode::kMaxLinks && g->skip[slot].load() == tag;
  }
};

class RingSink : public rns::ReticulumNode::Sink {
 public:
  // One channel packet from a confirmed link into the ring for cr-net, its link
  // tag ahead of it.
  void packet(const uint8_t* p, size_t len, uint32_t link_tag) override {
    // A joined carry packet can be up to 1284 bytes; nothing over the
    // channel's own limit is a channel packet, so it is dropped (and counted)
    // here rather than left in the ring for cr-net to trip over.
    if (len > packet::kMaxSize) {
      g->dropped_in++;
      return;
    }
    // The link's tag ahead of it, for cr-net's split horizon; written
    // straight into the ring.
    void* item = nullptr;
    if (xRingbufferSendAcquire(g->from_rns, &item, kInHead + len, 0) !=
            pdTRUE ||
        item == nullptr) {
      g->dropped_in++;
      return;
    }
    memcpy(item, &link_tag, kInHead);
    memcpy((uint8_t*)item + kInHead, p, len);
    xRingbufferSendComplete(g->from_rns, item);
  }
};

// Whatever was handed over while there is no connection is thrown away.
void drain() {
  size_t len;
  void* item;
  while ((item = xRingbufferReceive(g->to_rns, &len, 0)) != nullptr) {
    vRingbufferReturnItem(g->to_rns, item);
  }
}

// Waits ms, a tenth of a second at a time, or until the radio stops.
void wait(int64_t ms) {
  for (int64_t waited = 0; waited < ms && !g->config.stopped(); waited += 100) {
    drain();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// True while a connection attempt should go on: not stopped, and not shed
// by the memory guard or the clock (a TCP control block and its buffers are
// internal RAM, which is what the guard sheds Reticulum to keep).
bool may_go_on() { return !g->config.stopped() && g->config.may_connect(); }

// A connected, blocking socket to the node, or -1 with *why.
int open_connection(const char** why) {
  addrinfo hints = {};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  char port[8];
  snprintf(port, sizeof(port), "%u", (unsigned)g->config.port);
  addrinfo* found = nullptr;
  if (getaddrinfo(g->config.host.c_str(), port, &hints, &found) != 0 ||
      found == nullptr) {
    *why = "name not found";
    return -1;
  }
  // The name lookup can take seconds; whatever changed meanwhile wins.
  if (!may_go_on()) {
    freeaddrinfo(found);
    *why = "paused";
    return -1;
  }
  const int fd = socket(found->ai_family, SOCK_STREAM, IPPROTO_TCP);
  if (fd < 0) {
    freeaddrinfo(found);
    *why = "no socket";
    return -1;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  int rc = connect(fd, found->ai_addr, found->ai_addrlen);
  freeaddrinfo(found);
  if (rc != 0 && errno != EINPROGRESS) {
    ::close(fd);
    *why = errno == ECONNREFUSED ? "connection refused" : "connect failed";
    return -1;
  }
  // The connect, a second at a time, so a stop cuts it short.
  bool done = rc == 0;
  for (int64_t waited = 0; !done && waited < kConnectTimeoutMs;
       waited += 1000) {
    if (!may_go_on()) break;
    fd_set wr;
    FD_ZERO(&wr);
    FD_SET(fd, &wr);
    timeval tv = {1, 0};
    rc = select(fd + 1, nullptr, &wr, nullptr, &tv);
    if (rc < 0) break;
    if (rc > 0) {
      int err = 0;
      socklen_t len = sizeof(err);
      getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
      if (err != 0) {
        ::close(fd);
        *why = err == ECONNREFUSED ? "connection refused" : "no route";
        return -1;
      }
      done = true;
    }
  }
  if (!done) {
    ::close(fd);
    *why = may_go_on() ? "no answer" : "paused";
    return -1;
  }
  fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  // A connection that dies without a word, between packets: keep-alives.
  setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
  int idle = 10, interval = 5, count = 3;
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
  setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
  return fd;
}

// cr-rns: connects, reads the links, sends what the other tasks hand over a few
// packets a pass, and reconnects with a backoff.
void task(void*) {
  // The node's tables (~70 KB) and the deframer go to PSRAM: Arduino-ESP32
  // puts allocations over 4 KB there.
  SocketWriter* writer = new SocketWriter();
  RingSink sink;
  SkipView skip;
  rns::ReticulumNode* node =
      new rns::ReticulumNode(g->config.tag, g->config.confirm_key, writer);
  // A node whose identity or destination could not be hashed (the P4's
  // hardware SHA takes DMA RAM, which can run short) would announce a
  // destination nobody accepts, and stay unheard all session: another
  // identity a second later instead.
  while (!node->ok() && !g->config.stopped()) {
    ESP_LOGW(kTag, "Reticulum identity could not be hashed: trying again");
    delete node;
    wait(1000);
    node = new rns::ReticulumNode(g->config.tag, g->config.confirm_key, writer);
  }
  rns::Deframer* deframer = new rns::Deframer();
  static uint8_t buf[1024];  // One cr-rns
  int64_t backoff = kBackoffMinMs;
  uint32_t reconnects = 0;
  char note[96];
  while (!g->config.stopped()) {
    if (!g->config.may_connect()) {
      set_state(ReticulumStatus::State::kWaiting, "");
      wait(500);
      continue;
    }
    set_state(ReticulumStatus::State::kConnecting, "");
    const char* why = nullptr;
    const int fd = open_connection(&why);
    if (fd < 0) {
      if (g->config.stopped()) break;
      // Shed meanwhile: not a failure, so no backoff; back to waiting.
      if (!g->config.may_connect()) continue;
      snprintf(note, sizeof(note), "can't connect (%s); again in %lld s", why,
               (long long)(backoff / 1000));
      set_state(ReticulumStatus::State::kConnecting, note);
      ESP_LOGW(kTag, "%s: %s", g->status.where.c_str(), note);
      wait(backoff);
      backoff = backoff * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoff * 2;
      continue;
    }
    writer->reset(fd, mono_ms());
    *deframer = rns::Deframer();
    const int64_t connected_at = mono_ms();
    node->connected(connected_at);
    g->connected.store(true);
    set_state(ReticulumStatus::State::kConnected, "");
    ESP_LOGI(kTag, "Connected to %s", g->status.where.c_str());
    int64_t last_tick = connected_at;
    why = "closed";
    while (!g->config.stopped()) {
      if (!g->config.may_connect()) {
        why = "paused";
        break;
      }
      fd_set rd, wr;
      FD_ZERO(&rd);
      FD_SET(fd, &rd);
      FD_ZERO(&wr);
      if (writer->pending() > 0) FD_SET(fd, &wr);  // Woken when it takes more
      timeval tv = {0, 10 * 1000};  // What goes out waits at most 10 ms
      const int r = select(fd + 1, &rd, &wr, nullptr, &tv);
      if (r < 0) {
        why = "socket error";
        break;
      }
      if (r > 0 && FD_ISSET(fd, &rd)) {
        const int n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);
        if (n == 0) {
          why = "closed by the node";
          break;
        }
        if (n < 0) {
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            why = "receive failed";
            break;
          }
        } else {
          deframer->push(buf, (size_t)n, [&](const uint8_t* f, size_t len) {
            node->on_frame(f, len, mono_ms(), &sink);
          });
        }
      }
      // What the other tasks handed over, a few at a time (the socket read
      // and the memory guard checked between passes), old voice dropped.
      bool paused = false;
      for (int k = 0; k < kDrainBudget; k++) {
        if (!g->config.may_connect()) {
          paused = true;
          break;
        }
        size_t len;
        void* item = xRingbufferReceive(g->to_rns, &len, 0);
        if (item == nullptr) break;
        OutHead head;
        memcpy(&head, item, sizeof(head));
        const bool late = head.voice != 0 &&
                          (mono_ms() - head.at_ms > kVoiceMaxAgeMs ||
                           writer->pending() > kBacklogBytes);
        if (late) {
          g->dropped_late++;
        } else {
          // Split horizon for voice only: our hellos go on every link, so
          // each far end learns who we are.
          writer->set_voice(head.voice != 0, head.at_ms);
          node->send((const uint8_t*)item + sizeof(head), len - sizeof(head),
                     head.voice != 0 ? &skip : nullptr);
          writer->set_voice(false, 0);
        }
        vRingbufferReturnItem(g->to_rns, item);
      }
      if (paused) {
        why = "paused";
        break;
      }
      g->dropped_late += (uint32_t)writer->flush(mono_ms());
      const int64_t now = mono_ms();
      if (now - last_tick >= kTickMs) {
        last_tick = now;
        if (!node->tick(now)) {
          why = "silent";
          break;
        }
        publish(*node, reconnects);
      }
      g->linked.store(node->link_count() > 0);
      g->linked_remote.store(node->link_count(&skip) > 0);
      if (writer->broken) {
        why = writer->pending() > 0 ? "send stalled" : "send failed";
        break;
      }
    }
    // The polite closes first, while the socket still takes them.
    if (g->config.stopped() && !writer->broken) {
      node->close_all();
      writer->flush(mono_ms());  // As much of the goodbyes as goes now
    }
    node->disconnected(mono_ms());
    g->linked.store(false);
    g->linked_remote.store(false);
    g->connected.store(false);
    writer->reset(-1, mono_ms());
    for (auto& t : g->skip) t.store(0);  // Every link went with it
    ::close(fd);
    if (g->config.stopped()) break;
    reconnects++;
    publish(*node, reconnects);
    // Only a connection that held resets the backoff: one accepted and
    // dropped at once (rnsd restarting) must not be redialled every second.
    if (mono_ms() - connected_at >= kStableMs) backoff = kBackoffMinMs;
    snprintf(note, sizeof(note), "connection lost (%s); again in %lld s", why,
             (long long)(backoff / 1000));
    set_state(ReticulumStatus::State::kConnecting, note);
    ESP_LOGW(kTag, "%s: %s", g->status.where.c_str(), note);
    wait(backoff);
    backoff = backoff * 2 > kBackoffMaxMs ? kBackoffMaxMs : backoff * 2;
  }
  set_state(ReticulumStatus::State::kOff, "the radio stopped");
  ESP_LOGW(kTag, "Reticulum stopped with the radio");
  vTaskDelete(nullptr);
}

}  // namespace

// Creates the rings and cr-rns, then publishes the shared state to the other
// tasks.
bool start(const Config& config) {
  if (g != nullptr) return false;
  // Built whole before any other task can see it: cr-net reads the status
  // from its first turn, which may come before this.
  Shared* s = new Shared();
  s->config = config;
  s->lock = xSemaphoreCreateMutex();
  if (s->lock == nullptr) {
    ESP_LOGE(kTag, "Reticulum could not start (no lock): the WLAN alone");
    delete s;
    return false;  // status() stays Off
  }
  s->status.where = config.host.find(':') != std::string::npos
                        ? "[" + config.host + "]:" + std::to_string(config.port)
                        : config.host + ":" + std::to_string(config.port);
  s->to_rns = xRingbufferCreateWithCaps(kRingBytes, RINGBUF_TYPE_NOSPLIT,
                                        MALLOC_CAP_SPIRAM);
  s->from_rns = xRingbufferCreateWithCaps(kRingBytes, RINGBUF_TYPE_NOSPLIT,
                                          MALLOC_CAP_SPIRAM);
  s->status.state = ReticulumStatus::State::kWaiting;
  g = s;  // cr-rns's, from its first line
  bool ok = s->to_rns != nullptr && s->from_rns != nullptr &&
            xTaskCreatePinnedToCore(task, "cr-rns", kStack, nullptr, kPriority,
                                    nullptr, kCore) == pdPASS;
  if (!ok) {
    ESP_LOGE(kTag, "Reticulum could not start: the WLAN alone");
    // Nothing is handed over: the other tasks only ever see no rings.
    if (s->to_rns != nullptr) vRingbufferDeleteWithCaps(s->to_rns);
    if (s->from_rns != nullptr) vRingbufferDeleteWithCaps(s->from_rns);
    s->to_rns = s->from_rns = nullptr;
    s->status.state = ReticulumStatus::State::kOff;
    s->status.note = "could not start";
  } else {
    ESP_LOGI(kTag, "Reticulum via %s", s->status.where.c_str());
  }
  g_ready.store(s, std::memory_order_release);
  return ok;
}

// Hands a sealed channel packet to cr-rns through the ring, with its time and
// whether it is voice.
bool send(const uint8_t* packet, size_t len, bool voice) {
  Shared* s = ready();
  if (s == nullptr || s->to_rns == nullptr || !s->linked.load() ||
      len > packet::kMaxSize) {
    return false;
  }
  OutHead head = {};
  head.at_ms = mono_ms();
  head.voice = voice ? 1 : 0;
  // Written straight into the ring: nothing on the caller's stack.
  void* item = nullptr;
  if (xRingbufferSendAcquire(s->to_rns, &item, sizeof(head) + len, 0) !=
          pdTRUE ||
      item == nullptr) {
    s->dropped_out++;
    return false;
  }
  memcpy(item, &head, sizeof(head));
  memcpy((uint8_t*)item + sizeof(head), packet, len);
  xRingbufferSendComplete(s->to_rns, item);
  return true;
}

// cr-net's split horizon: the link `tag` in `slot` gets nothing (0: it does).
void set_skip(int slot, uint32_t tag) {
  Shared* s = ready();
  if (s == nullptr || slot < 0 || slot >= rns::ReticulumNode::kMaxLinks) return;
  s->skip[slot].store(tag);
}

// The next channel packet from Reticulum and the tag of the link it came on, or
// 0 when none waits.
size_t receive(uint8_t* out, size_t cap, uint32_t* link_tag) {
  Shared* s = ready();
  if (s == nullptr || s->from_rns == nullptr) return 0;
  // 0 only when the ring is empty: an item too large for `cap` is dropped
  // and the next one taken, so it never ends the caller's drain early.
  for (;;) {
    size_t len = 0;
    void* item = xRingbufferReceive(s->from_rns, &len, 0);
    if (item == nullptr) return 0;
    const bool fits = len > kInHead && len - kInHead <= cap;
    if (fits) {
      memcpy(link_tag, item, kInHead);
      memcpy(out, (const uint8_t*)item + kInHead, len - kInHead);
    }
    vRingbufferReturnItem(s->from_rns, item);
    if (fits) return len - kInHead;
    s->dropped_in++;
  }
}

// True while cr-rns is connected to the transport node.
bool connected() {
  Shared* s = ready();
  return s != nullptr && s->connected.load();
}

// True while cr-rns has at least one confirmed link.
bool linked() {
  Shared* s = ready();
  return s != nullptr && s->linked.load();
}

// True while some confirmed link's far end is off our own WLAN.
bool linked_remote() {
  Shared* s = ready();
  return s != nullptr && s->linked_remote.load();
}

ReticulumStatus status() {
  ReticulumStatus out;
  Shared* s = ready();
  if (s == nullptr) return out;  // Not started: Off
  xSemaphoreTake(s->lock, portMAX_DELAY);
  out = s->status;
  xSemaphoreGive(s->lock);
  return out;
}

}  // namespace reticulum
}  // namespace esp
}  // namespace crewradio

#endif
