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
  // Signal K client generation of the last sample, and the end of the
  // replay window after the first sample of a new connection (see
  // SkData::store).
  uint32_t conn_gen = 0;
  uint32_t replay_until_ms = 0;

  bool fresh(uint32_t now_ms) const {
    return seen && (now_ms - updated_ms) < kStaleAfterMs;
  }
};

}  // namespace skgauges
