#pragma once

// Board-independent start-up: SensESP (WiFi, Signal K, OTA, web config) and
// the gauge UI. Each board's main.cpp brings up its display and touch
// through ESP32_Display_Panel and lvgl_port_init(), then calls
// gauge_app_begin() from setup() and gauge_app_loop() from loop().

#include <functional>

#include "gauge_defs.h"

namespace skgauges {

// The board, not the unit: one firmware per board serves both sides. Which
// unit it is (side, hostname, name) comes from the hostname saved on it
// (unit_identity.h), so a build is never per side.
struct GaugeAppConfig {
  const char* board;        // In the hostname: "4c" -> "gauge-4c-port"
  const char* board_label;  // In the unit name: "4in" -> "4in PORT"
  // Each side's address (its SECRET_OTA_HOST_* entry). Used as a static IP
  // when secrets.h sets SECRET_USE_STATIC_IP to 1.
  const char* address_stbd;
  const char* address_port;
  int button_pin;           // SensESP's button input (-1 = none)
  std::function<void(int percent)> set_brightness;
};

void gauge_app_begin(const GaugeAppConfig& config);
void gauge_app_loop();

}  // namespace skgauges
