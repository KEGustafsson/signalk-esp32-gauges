#pragma once

// The latest reading for one gauge page. Kept free of Arduino and SensESP so
// the UI code (common/src/ui) and tools/preview can use it on the host.

#include <math.h>
#include <stdint.h>

#include "gauge_defs.h"

namespace skgauges {

struct GaugeValue {
  float value = NAN;   // Displayed units (attitude: roll / heel in degrees)
  float value2 = NAN;  // Attitude only: pitch / trim in degrees
  uint32_t updated_ms = 0;
  // A usable sample is held. Cleared on an invalid (null) delta and by a
  // periodic sweep once the value is stale, so a millis() wraparound after
  // 49.7 days cannot make an old value look fresh again.
  bool seen = false;
  // The Signal K connection the last sample came from, and the replay
  // window opened by the first sample of each new connection (see
  // value_gate.h). conn_known is false until the first sample, so the very
  // first connection after boot opens a window too.
  bool conn_known = false;
  uint32_t conn_gen = 0;
  uint32_t conn_epoch = 0;
  bool replaying = false;
  uint32_t replay_until_ms = 0;
  // A null arrived while a recent valid value was held: blank once it has
  // gone kInvalidGraceMs without a valid sample (see value_gate.h).
  bool invalid_pending = false;
  uint32_t invalid_ms = 0;

  // The page's data timeout (its settings card), kStaleAfterMs by default.
  uint32_t stale_ms = kStaleAfterMs;

  bool fresh(uint32_t now_ms) const {
    return seen && (now_ms - updated_ms) < stale_ms;
  }
};

}  // namespace skgauges
