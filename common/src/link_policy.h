#pragma once

// When a gauge restarts because a link stopped working: the rules behind
// AGENTS.md's "Recover, don't freeze", adopted from espOS (skLinkStalled and
// its ESP-Hosted heartbeat watchdog) and kept free of Arduino, SensESP and
// ESP-IDF so they are unit-tested on the host (tests/). link_watch.cpp feeds
// them the clock and the links' states.

#include <stdint.h>

namespace skgauges {

// Signal K unreachable this long with WiFi up, after it worked this boot:
// restart. SensESP reconnects on its own; this is for a client that stopped
// trying. Not before it first connected, so a server that is down does not
// restart a gauge over and over.
constexpr uint32_t kSkStallMs = 300000;

struct SkStallWatch {
  bool ever_up = false;     // Signal K connected at least once this boot
  bool waiting = false;     // Down with WiFi up, since `since_ms`
  uint32_t since_ms = 0;
};

// Called every few seconds. True: restart now.
inline bool sk_stall_check(SkStallWatch* w, bool wifi_up, bool sk_up,
                           uint32_t now_ms) {
  if (sk_up) {
    w->ever_up = true;
    w->waiting = false;
    return false;
  }
  if (!w->ever_up || !wifi_up) {
    // WiFi down is SensESP's WiFi watchdog's to judge; the wait starts over
    // when WiFi is back.
    w->waiting = false;
    return false;
  }
  if (!w->waiting) {
    w->waiting = true;
    w->since_ms = now_ms;
    return false;
  }
  return now_ms - w->since_ms >= kSkStallMs;
}

// The 4"'s WiFi chip (ESP32-C6, ESP-Hosted over SDIO) is asked for a
// heartbeat every kHostedBeatS seconds; none for kHostedSilenceMs means the
// link is gone, even when nothing on the P4 noticed (a wedged link raises no
// error, the WiFi state still says connected). espOS's values.
constexpr int kHostedBeatS = 20;
constexpr uint32_t kHostedSilenceMs = 60000;

enum class HostedEvent {
  kHeartbeat,         // ESP_HOSTED_EVENT_CP_HEARTBEAT
  kCoprocessorStart,  // ESP_HOSTED_EVENT_CP_INIT: the C6 (re)started
  kTransportFailure,  // ESP_HOSTED_EVENT_TRANSPORT_FAILURE
};

struct HostedWatch {
  bool beat_seen = false;  // A heartbeat arrived this boot
};

// True: restart now. The C6 announces its start once at every boot, before
// any heartbeat: that one is the boot handshake (espOS restarted on it and
// the P4 boot-looped, rolling back an update). After a heartbeat, a start
// means the C6 reset under a running link, which the P4 cannot pick up.
inline bool hosted_event(HostedWatch* w, HostedEvent e) {
  switch (e) {
    case HostedEvent::kHeartbeat:
      w->beat_seen = true;
      return false;
    case HostedEvent::kCoprocessorStart:
      return w->beat_seen;
    case HostedEvent::kTransportFailure:
      return true;
  }
  return false;
}

}  // namespace skgauges
