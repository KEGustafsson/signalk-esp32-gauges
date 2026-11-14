// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Day, dusk and night colours. Every colour the pages draw is written for
// the day (the classic steel look) and passes through tone_colour() on its
// way to LVGL (ui::ink(), draw_utils.h), so the three looks are one rule
// each, not three sets of colours to keep in step:
//
//  - day: as written;
//  - dusk: dimmed, the blue most, so the screen is softer and warmer while
//    every colour still reads as itself (a zone stays yellow or red);
//  - night: red on black, for eyes adapted to the dark. A colour's
//    brightest channel sets how bright a red it becomes, squared, so dark
//    areas (the face, the bezel's shadows) go close to black and only the
//    markings, the needle and the digits stay bright. Hues are gone: the
//    red needle is as bright as a white marking, and a zone shows as a
//    brighter band, not a colour.
//
// Black stays black in every mode. Pure and header-only, host-tested
// (tests/test_colour_mode.cpp).

#include <stdint.h>

namespace skgauges {

enum class ColourMode : uint8_t { kDay = 0, kDusk = 1, kNight = 2 };
constexpr int kColourModeCount = 3;

inline const char* colour_mode_name(ColourMode mode) {
  switch (mode) {
    case ColourMode::kDusk:
      return "Dusk";
    case ColourMode::kNight:
      return "Night";
    default:
      return "Day";
  }
}

// The mode stored as `value` (NVS), or day for anything else.
inline ColourMode colour_mode_from_int(int value) {
  return value > 0 && value < kColourModeCount ? (ColourMode)value
                                               : ColourMode::kDay;
}

namespace colour_mode_detail {
// Dusk: each channel times this / 256.
constexpr uint32_t kDuskRed = 184;    // 72 %
constexpr uint32_t kDuskGreen = 159;  // 62 %
constexpr uint32_t kDuskBlue = 128;   // 50 %
// Night: the brightest a red goes (a white marking), of 255.
constexpr uint32_t kNightPeak = 220;
}  // namespace colour_mode_detail

// `rgb` (0xRRGGBB, a day colour) as it is drawn in `mode`.
inline uint32_t tone_colour(uint32_t rgb, ColourMode mode) {
  using namespace colour_mode_detail;
  const uint32_t r = (rgb >> 16) & 0xFF;
  const uint32_t g = (rgb >> 8) & 0xFF;
  const uint32_t b = rgb & 0xFF;
  switch (mode) {
    case ColourMode::kDusk:
      return ((r * kDuskRed) >> 8) << 16 | ((g * kDuskGreen) >> 8) << 8 |
             ((b * kDuskBlue) >> 8);
    case ColourMode::kNight: {
      const uint32_t v = r > g ? (r > b ? r : b) : (g > b ? g : b);
      return ((v * v * kNightPeak + 255 * 255 / 2) / (255 * 255)) << 16;
    }
    default:
      return rgb & 0xFFFFFF;
  }
}

}  // namespace skgauges
