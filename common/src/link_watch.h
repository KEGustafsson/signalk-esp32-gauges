#pragma once

// Watches the links a gauge depends on and restarts it when one has stopped
// working for good (link_policy.h has the rules): adopted from espOS.
//  - WiFi power save off (before WiFi starts);
//  - Signal K unreachable with WiFi up, after it worked this boot;
//  - on the 4", the ESP32-C6's heartbeat and ESP-Hosted's link events.

namespace skgauges {

// Before SensESP starts WiFi.
void link_watch_before_wifi();

// After SensESP's app exists.
void link_watch_begin();

}  // namespace skgauges
