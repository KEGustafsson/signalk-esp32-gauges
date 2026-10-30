#pragma once

// Board-independent start-up: SensESP (WiFi, Signal K, OTA, web config) and
// the gauge UI. Each board's main.cpp brings up its display and touch
// through ESP32_Display_Panel and lvgl_port_init(), then calls
// gauge_app_begin() from setup() and gauge_app_loop() from loop().

#include <functional>

#include "gauge_defs.h"

namespace skgauges {

struct GaugeAppConfig {
  const char* hostname;   // mDNS name, SensESP AP name, SK client name
  const char* unit_name;  // Human label shown in the info overlay
  Side side;              // Which engine this unit shows
  // This unit's address (its SECRET_OTA_HOST_* entry). Used as a static IP
  // when secrets.h sets SECRET_USE_STATIC_IP to 1.
  const char* address;
  int button_pin;         // SensESP's button input (-1 = none)
  std::function<void(int percent)> set_brightness;
};

void gauge_app_begin(const GaugeAppConfig& config);
void gauge_app_loop();

}  // namespace skgauges
