#pragma once

// Latest Signal K values for every gauge page, fed by SensESP listeners.
//
// Threading: SensESP's WebSocket client queues deltas and hands them to the
// listeners from the event loop (SKWSClient::process_received_updates runs
// via event_loop()->onRepeat). The boards' loop() runs that event loop and
// lv_timer_handler() back to back on the same task, so the UI reads these
// values without locking.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "gauge_defs.h"
#include "gauge_value.h"

namespace skgauges {

class SkData {
 public:
  // Subscribes to the SK path of every page this unit shows. Call after
  // SensESPAppBuilder's get_app().
  void begin(Side unit_side);

  const GaugeValue& get(size_t index) const { return values_[index]; }

  // True while SensESP's WebSocket to the Signal K server is up.
  bool server_connected() const;

  // The SK path page `index` currently listens to (as configured in the web
  // UI), or "" if this unit does not show that page.
  String sk_path(size_t index) const;

 private:
  void store(GaugeValue* slot, float value, float value2);

  GaugeValue values_[kMaxGauges];
  std::function<String()> paths_[kMaxGauges];
  uint32_t conn_gen_ = 1;
};

SkData& sk_data();

}  // namespace skgauges
