#pragma once

// Page manager and touch handling.
//
//  - Swipe left / right: next / previous gauge (wraps around).
//  - Tap: info overlay (unit, IP, Signal K connection) for a few seconds.
//  - Long press: cycle backlight brightness 100 / 60 / 30 / 10 %.
//
// The current page and brightness survive a reboot (NVS).

#include <stdint.h>

#include <functional>

#include "gauge_defs.h"

namespace skgauges {

struct UiConfig {
  Side side;               // Which engine's pages this unit shows
  const char* unit_name;   // Shown in the info overlay, e.g. "4\" STBD"
  std::function<void(int percent)> set_brightness;
};

// Call once after the LVGL display and input device exist.
void ui_begin(const UiConfig& config);

// SensESP's system status "LED" colour, mirrored as a small dot at the
// bottom of the bezel (see status_led.h).
void ui_set_status_color(uint8_t r, uint8_t g, uint8_t b);

}  // namespace skgauges
