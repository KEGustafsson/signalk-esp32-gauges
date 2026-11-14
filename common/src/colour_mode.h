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
// Black stays black in every mode. The mode is chosen on the unit (the
// swipe-down box) or follows Signal K (SkColourMode, below). Pure and
// header-only, host-tested (tests/test_colour_mode.cpp).

#include <stdint.h>
#include <string.h>

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

// ---- The mode from Signal K ----
//
// Two paths say how light it is, both written by the server's derived-data
// plugin (its "Sun" calculation, from navigation.position and .datetime):
//
//  - environment.mode, in the Signal K specification: "day", "night" or
//    "restricted visibility". derived-data sets it at sunrise and sunset
//    only; the App Dock's day / night button sets it by hand.
//  - environment.sun, not in the specification, derived-data's: the sun's
//    phase, written every minute: "nauticalDawn", "dawn", "sunrise", "day",
//    "sunset", "dusk", "nauticalDusk", "night".

// environment.mode's value as a mode: day and night; false for anything
// else (restricted visibility, unknown).
inline bool colour_mode_from_sk_mode(const char* value, ColourMode* mode) {
  if (value == nullptr) return false;
  if (strcmp(value, "day") == 0) {
    *mode = ColourMode::kDay;
  } else if (strcmp(value, "night") == 0) {
    *mode = ColourMode::kNight;
  } else {
    return false;
  }
  return true;
}

// environment.sun's phase as a mode, by how dark it is: the sun up (day,
// sunrise) is day; civil twilight, the sun up to 6 degrees below the horizon
// (dawn, sunset), is dusk; nautical twilight and darker (nauticalDawn,
// dusk, nauticalDusk, night) is night. False for anything else.
inline bool colour_mode_from_sk_sun(const char* value, ColourMode* mode) {
  if (value == nullptr) return false;
  struct Phase {
    const char* name;
    ColourMode mode;
  };
  static const Phase kPhases[] = {
      {"day", ColourMode::kDay},           {"sunrise", ColourMode::kDay},
      {"dawn", ColourMode::kDusk},         {"sunset", ColourMode::kDusk},
      {"nauticalDawn", ColourMode::kNight}, {"dusk", ColourMode::kNight},
      {"nauticalDusk", ColourMode::kNight}, {"night", ColourMode::kNight},
  };
  for (const Phase& p : kPhases) {
    if (strcmp(value, p.name) == 0) {
      *mode = p.mode;
      return true;
    }
  }
  return false;
}

// What the two paths ask for, and when the unit should switch to it.
//
// The sun's phase decides when the server has one (it has three steps, the
// mode two). environment.mode decides instead only when it changes to
// contradict the sun outright -- to night while the sun says day, or to day
// while it says night: that is someone's hand on the App Dock's button. It
// holds until the sun's next step. A mode that merely stays as it was
// overrides nothing: derived-data leaves it at night until sunrise, while
// the sun's phase has already moved on to dawn and sunrise. Nor do
// derived-data's own switches contradict (night at sunset falls in the
// sun's dusk). Without a phase, environment.mode decides alone.
//
// The unit switches only when that answer changes, never because a value
// came again: derived-data restates the phase every minute and the server
// replays both on every reconnect, and neither may undo a mode chosen on the
// unit's own buttons, which holds until the next change of the answer. The
// first values (at boot, in either order) override nothing: who set them is
// unknown by then. A value that means nothing here (null, "restricted
// visibility", unknown) changes nothing.
class SkColourMode {
 public:
  // A value of environment.mode / environment.sun. True, with *switch_to,
  // when the unit should switch.
  bool on_mode(const char* value, ColourMode* switch_to) {
    ColourMode m;
    if (!colour_mode_from_sk_mode(value, &m)) return false;
    if (have_mode_ && m != mode_) {
      by_hand_ = have_sun_ && contradicts(m, sun_);
    }
    mode_ = m;
    have_mode_ = true;
    return decide(switch_to);
  }
  // The answer given before a restart (saved by the caller): the first
  // values after it then switch nothing unless they answer otherwise, so a
  // mode chosen on the buttons survives a restart as long as the light has
  // not changed meanwhile.
  void seed(ColourMode last_answer) {
    wanted_ = last_answer;
    have_wanted_ = true;
  }
  // The last answer given (for the caller to save); false before any.
  bool answer(ColourMode* last) const {
    if (have_wanted_) *last = wanted_;
    return have_wanted_;
  }

  bool on_sun(const char* value, ColourMode* switch_to) {
    ColourMode m;
    if (!colour_mode_from_sk_sun(value, &m)) return false;
    if (!have_sun_ || m != sun_) {
      by_hand_ = false;  // The sun's next step ends a switch by hand
    }
    sun_ = m;
    have_sun_ = true;
    return decide(switch_to);
  }

 private:
  static bool contradicts(ColourMode mode, ColourMode sun) {
    return (mode == ColourMode::kNight && sun == ColourMode::kDay) ||
           (mode == ColourMode::kDay && sun == ColourMode::kNight);
  }
  bool decide(ColourMode* switch_to) {
    const ColourMode wanted = !have_sun_ || by_hand_ ? mode_ : sun_;
    if (have_wanted_ && wanted == wanted_) return false;
    wanted_ = wanted;
    have_wanted_ = true;
    *switch_to = wanted;
    return true;
  }

  ColourMode mode_ = ColourMode::kDay;
  ColourMode sun_ = ColourMode::kDay;
  ColourMode wanted_ = ColourMode::kDay;
  bool have_mode_ = false;
  bool have_sun_ = false;
  bool have_wanted_ = false;
  bool by_hand_ = false;  // environment.mode changed against the sun
};

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
