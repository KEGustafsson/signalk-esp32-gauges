#pragma once

// Latest Signal K values for every gauge page, fed by SensESP listeners.
//
// Threading: SensESP's WebSocket client queues deltas and hands them to the
// listeners from the event loop (SKWSClient::process_received_updates runs
// via event_loop()->onRepeat). The boards' loop() runs that event loop and
// lv_timer_handler() back to back on the same task, so the UI reads these
// values without locking.

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "gauge_defs.h"

namespace skgauges {

struct GaugeValue {
  float value = NAN;   // Displayed units (attitude: roll / heel in degrees)
  float value2 = NAN;  // Attitude only: pitch / trim in degrees
  uint32_t updated_ms = 0;
  bool seen = false;   // A sample has arrived at least once

  bool fresh(uint32_t now_ms) const {
    return seen && (now_ms - updated_ms) < kStaleAfterMs;
  }
};

class SkData {
 public:
  // Subscribes to the SK path of every page this unit shows. Call after
  // SensESPAppBuilder's get_app().
  void begin(Side unit_side);

  const GaugeValue& get(size_t index) const { return values_[index]; }

  // True while SensESP's WebSocket to the Signal K server is up.
  bool server_connected() const;

 private:
  GaugeValue values_[kMaxGauges];
};

SkData& sk_data();

}  // namespace skgauges
