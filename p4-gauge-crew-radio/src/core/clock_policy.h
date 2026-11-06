// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Whether the node's wall clock can be trusted, and from where. Every packet
// more than 60 s off the receiver's clock is stale, so without a trusted
// clock the radio stays off ("no clock") rather than send packets nobody
// accepts or drop everything it hears.
//
// Two sources set the system clock: SNTP (the router or the Signal K host)
// and the boat's GNSS through Signal K (navigation.datetime). Signal K keeps
// a path's last value for ever and sends it again on every subscribe, so a
// GNSS time is believed only when it moves: a reading counts when, against
// the one before it, it has advanced by the monotonic time between them
// (within kStepS), 0.5-60 s apart. A dead receiver's frozen time, or the
// retained value after a reconnect, never sets the clock. A sync is
// trusted for kTrustMs after it (an ESP32 crystal drifts a few seconds a
// day, far inside the window). GNSS steps the clock when it is more than
// kStepS off, unless SNTP synced within kSntpPreferMs. Never from the
// channel: adopting other nodes' packet times would let a recording set the
// clock back, which is the replay the window exists to stop.
//
// `mono_ms` is a monotonic clock (millis since boot), `system_s` the wall
// clock. Pure; owned by the supervisor task.

#include <stdint.h>

namespace crewradio {

enum class ClockSource : uint8_t { kNone, kSntp, kGnss };

// Signal K's navigation.datetime, "2026-09-07T12:00:00.000Z" (an offset
// "+03:00" in place of the Z is accepted, fractions ignored): seconds since
// the epoch. False for anything else, a time without a zone included.
bool parse_iso8601_utc(const char* text, int64_t* epoch_s);

class ClockPolicy {
 public:
  static constexpr int64_t kMinEpochS = 1767225600;  // 2026-01-01: unset before
  static constexpr int64_t kTrustMs = 24LL * 3600 * 1000;
  static constexpr int64_t kSntpPreferMs = 3600LL * 1000;
  static constexpr int64_t kStepS = 2;

  void sntp_synced(int64_t mono_ms);
  // A GNSS time as it arrived; true if the caller should set the system
  // clock to it.
  bool gnss(int64_t gnss_s, int64_t system_s, int64_t mono_ms);
  // The Signal K connection went away: the next reading starts over.
  void gnss_lost() { has_reading_ = false; }
  // The source of the clock now, kNone if it is not to be trusted.
  ClockSource source(int64_t mono_ms, int64_t system_s) const;

 private:
  static bool recent(bool has, int64_t at, int64_t now, int64_t limit) {
    return has && now - at <= limit && now >= at;
  }
  bool has_reading_ = false;
  int64_t reading_s_ = 0;
  int64_t reading_ms_ = 0;
  bool has_sntp_ = false;
  bool has_gnss_ = false;
  int64_t sntp_ms_ = 0;
  int64_t gnss_ms_ = 0;
};

}  // namespace crewradio
