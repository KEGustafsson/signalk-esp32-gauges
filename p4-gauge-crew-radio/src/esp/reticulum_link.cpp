// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
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
constexpr int kSendTimeoutS = 2;

int64_t mono_ms() { return esp_timer_get_time() / 1000; }

struct Shared {
  Config config;
  RingbufHandle_t to_rns = nullptr;
  RingbufHandle_t from_rns = nullptr;
  std::atomic<bool> connected{false};
  std::atomic<bool> linked{false};
  std::atomic<uint32_t> dropped_in{0};
  std::atomic<uint32_t> dropped_out{0};
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
  xSemaphoreGive(g->lock);
}

// Writes each Reticulum packet framed, whole or not at all: a send that
// fails, or a frame not out within kSendTimeoutS in all, breaks the
// connection, which is then opened again. SO_SNDTIMEO alone only catches a
// send that makes no progress: lwIP returns what it got out when the time
// is up, so an uplink that trickles a few bytes every 2 s would hold cr-rns
// in here for good, its links unread and its ring overflowing (the app
// closes a connection whose queue is stuck full; this is the same rule).
class SocketWriter : public rns::ReticulumNode::Writer {
 public:
  int fd = -1;
  bool broken = false;
  void write(const uint8_t* raw, size_t len) override {
    if (fd < 0 || broken) return;
    const size_t n = rns::frame(raw, len, frame_);
    const int64_t deadline = mono_ms() + kSendTimeoutS * 1000;
    size_t off = 0;
    while (off < n) {
      const int w = ::send(fd, frame_ + off, n - off, 0);
      if (w <= 0 || (off + (size_t)w < n && mono_ms() >= deadline)) {
        broken = true;
        return;
      }
      off += (size_t)w;
    }
  }

 private:
  uint8_t frame_[rns::kMaxFrame];
};

class RingSink : public rns::ReticulumNode::Sink {
 public:
  void packet(const uint8_t* p, size_t len) override {
    // A joined carry packet can be up to 1284 bytes; nothing over the
    // channel's own limit is a channel packet, so it is dropped (and counted)
    // here rather than left in the ring for cr-net to trip over.
    if (len > packet::kMaxSize ||
        xRingbufferSend(g->from_rns, p, len, 0) != pdTRUE) {
      g->dropped_in++;
    }
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
  timeval send_timeout = {kSendTimeoutS, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
  return fd;
}

void task(void*) {
  // The node's tables (~70 KB) and the deframer go to PSRAM: Arduino-ESP32
  // puts allocations over 4 KB there.
  SocketWriter* writer = new SocketWriter();
  RingSink sink;
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
    writer->fd = fd;
    writer->broken = false;
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
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(fd, &rd);
      timeval tv = {0, 10 * 1000};  // What goes out waits at most 10 ms
      const int r = select(fd + 1, &rd, nullptr, nullptr, &tv);
      if (r < 0) {
        why = "socket error";
        break;
      }
      if (r > 0) {
        const int n = recv(fd, buf, sizeof(buf), 0);
        if (n == 0) {
          why = "closed by the node";
          break;
        }
        if (n < 0) {
          if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
          why = "receive failed";
          break;
        }
        deframer->push(buf, (size_t)n, [&](const uint8_t* f, size_t len) {
          node->on_frame(f, len, mono_ms(), &sink);
        });
      }
      size_t len;
      void* item;
      while ((item = xRingbufferReceive(g->to_rns, &len, 0)) != nullptr) {
        node->send((const uint8_t*)item, len);
        vRingbufferReturnItem(g->to_rns, item);
      }
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
      if (writer->broken) {
        why = "send failed";
        break;
      }
    }
    // The polite closes first, while the socket still takes them.
    if (g->config.stopped() && !writer->broken) node->close_all();
    node->disconnected(mono_ms());
    g->linked.store(false);
    g->connected.store(false);
    writer->fd = -1;
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

bool send(const uint8_t* packet, size_t len) {
  Shared* s = ready();
  if (s == nullptr || s->to_rns == nullptr || !s->linked.load()) return false;
  if (xRingbufferSend(s->to_rns, packet, len, 0) != pdTRUE) {
    s->dropped_out++;
    return false;
  }
  return true;
}

size_t receive(uint8_t* out, size_t cap) {
  Shared* s = ready();
  if (s == nullptr || s->from_rns == nullptr) return 0;
  // 0 only when the ring is empty: an item too large for `cap` is dropped
  // and the next one taken, so it never ends the caller's drain early.
  for (;;) {
    size_t len = 0;
    void* item = xRingbufferReceive(s->from_rns, &len, 0);
    if (item == nullptr) return 0;
    const bool fits = len > 0 && len <= cap;
    if (fits) memcpy(out, item, len);
    vRingbufferReturnItem(s->from_rns, item);
    if (fits) return len;
    s->dropped_in++;
  }
}

bool connected() {
  Shared* s = ready();
  return s != nullptr && s->connected.load();
}

bool linked() {
  Shared* s = ready();
  return s != nullptr && s->linked.load();
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
