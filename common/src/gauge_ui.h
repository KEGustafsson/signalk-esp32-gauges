#pragma once

// Page manager and touch handling.
//
//  - Swipe left / right: next / previous gauge (wraps around).
//  - Swipe down: info overlay (unit, IP, Signal K connection) for a few
//    seconds, with Day / Dusk / Night buttons for the colour mode
//    (colour_mode.h); swipe up or tap closes it (a tap alone opens nothing).
//  - Long press: cycle backlight brightness 100 / 60 / 30 / 10 %.
//
// The current page, brightness and colour mode survive a reboot (NVS), with
// the last colour mode Signal K asked for.

#include <stdint.h>

#include <functional>

#include "colour_mode.h"
#include "gauge_defs.h"

namespace skgauges {

struct UiConfig {
  const char* unit_name;   // Shown in the info overlay, e.g. "4\" STBD"
  std::function<void(int percent)> set_brightness;
};

// Call once after the LVGL display and input device exist.
void ui_begin(const UiConfig& config);

// Replaces the gauge page with a full-screen notice and draws it at once
// (used while an OTA upload blocks the loop).
void ui_show_notice(const char* text);

// Signal K's environment.mode / environment.sun asked for `mode`
// (sk_data.h): switches to it, as a tap on its button does, and saves it as
// the last answer from Signal K.
void ui_follow_signalk_colour(ColourMode mode);
// The last answer from Signal K before this boot (NVS); false if none. The
// caller seeds SkColourMode with it, so a mode chosen on the buttons
// survives a restart unless the light changed meanwhile.
bool ui_saved_signalk_colour(ColourMode* mode);

// SensESP's system status "LED" colour, mirrored as a small dot at the
// bottom of the bezel (see status_led.h).
void ui_set_status_color(uint8_t r, uint8_t g, uint8_t b);

}  // namespace skgauges
