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

#include <memory>
#include <vector>

#include "gauge_defs.h"
#include "gauge_value.h"
#include "sensesp/signalk/signalk_listener.h"

namespace skgauges {

class SkData {
 public:
  // Subscribes to the SK path of every page switched on. Call after
  // SensESPAppBuilder's get_app() and gauge_config().begin().
  void begin();

  const GaugeValue& get(size_t index) const { return values_[index]; }

  // True while SensESP's WebSocket to the Signal K server is up.
  bool server_connected() const;

 private:
  void store(GaugeValue* slot, float value, float value2);
  void invalid(GaugeValue* slot);

  GaugeValue values_[kMaxPages];
  // Counts connection state changes, as seen on the loop. SensESP's client
  // generation changes only when it tears a client down itself; the
  // WebSocket library also reconnects the same client on its own.
  uint32_t conn_epoch_ = 0;
  std::vector<std::shared_ptr<sensesp::SKListener>> listeners_;
};

SkData& sk_data();

}  // namespace skgauges
