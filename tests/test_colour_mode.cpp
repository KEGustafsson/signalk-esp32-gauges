// SPDX-License-Identifier: EUPL-1.2
// Day, dusk and night colours (colour_mode.h).
#include "colour_mode.h"
#include "test.h"

using skgauges::ColourMode;
using skgauges::colour_mode_from_int;
using skgauges::tone_colour;

namespace {
uint32_t red(uint32_t rgb) { return (rgb >> 16) & 0xFF; }
uint32_t green(uint32_t rgb) { return (rgb >> 8) & 0xFF; }
uint32_t blue(uint32_t rgb) { return rgb & 0xFF; }
}  // namespace

TEST(colour_day_draws_every_colour_as_written) {
  for (uint32_t rgb : {0x000000u, 0xFFFFFFu, 0xE0141Au, 0x55555Au, 0xC9D1B9u}) {
    CHECK(tone_colour(rgb, ColourMode::kDay) == rgb);
  }
  CHECK(tone_colour(0xFF123456, ColourMode::kDay) == 0x123456);  // No alpha
}

TEST(colour_black_stays_black_in_every_mode) {
  CHECK(tone_colour(0x000000, ColourMode::kDay) == 0);
  CHECK(tone_colour(0x000000, ColourMode::kDusk) == 0);
  CHECK(tone_colour(0x000000, ColourMode::kNight) == 0);
}

TEST(colour_dusk_dims_every_channel_the_blue_most_and_keeps_hues) {
  const uint32_t white = tone_colour(0xFFFFFF, ColourMode::kDusk);
  CHECK(red(white) < 0xFF && green(white) < red(white) &&
        blue(white) < green(white));
  CHECK(blue(white) > 0x60);  // Dimmed, still a light colour
  // A zone keeps its colour: yellow stays more red and green than blue,
  // red stays red.
  const uint32_t yellow = tone_colour(0xFFFF60, ColourMode::kDusk);
  CHECK(red(yellow) > blue(yellow) && green(yellow) > blue(yellow));
  const uint32_t alarm = tone_colour(0xDD0000, ColourMode::kDusk);
  CHECK(red(alarm) > 0x90 && green(alarm) == 0 && blue(alarm) == 0);
  // Never brighter than day, channel by channel.
  for (uint32_t rgb : {0x0164FAu, 0x9CCBEAu, 0x81C784u, 0x7F7F7Fu}) {
    const uint32_t t = tone_colour(rgb, ColourMode::kDusk);
    CHECK(red(t) <= red(rgb) && green(t) <= green(rgb) && blue(t) <= blue(rgb));
  }
}

TEST(colour_night_is_red_only_and_dark_areas_go_darker_still) {
  for (uint32_t rgb : {0xFFFFFFu, 0x0164FAu, 0x4CD964u, 0x9CCBEAu, 0x55555Au}) {
    const uint32_t t = tone_colour(rgb, ColourMode::kNight);
    CHECK(green(t) == 0 && blue(t) == 0);
    CHECK(red(t) > 0);
  }
  CHECK(red(tone_colour(0xFFFFFF, ColourMode::kNight)) == 220);
  // The needle (red) stays about as bright as a white marking, not as dim as
  // its luminance would make it.
  CHECK(red(tone_colour(0xE0141A, ColourMode::kNight)) > 160);
  // The face (mid grey) goes far darker than in proportion: squared.
  CHECK(red(tone_colour(0x55555A, ColourMode::kNight)) < 0x20);
  // Brighter in, never darker out.
  uint32_t last = 0;
  for (uint32_t v = 0; v <= 0xFF; v++) {
    const uint32_t t = red(tone_colour(v << 8, ColourMode::kNight));
    CHECK(t >= last);
    last = t;
  }
}

TEST(colour_mode_from_storage_falls_back_to_day) {
  CHECK(colour_mode_from_int(0) == ColourMode::kDay);
  CHECK(colour_mode_from_int(1) == ColourMode::kDusk);
  CHECK(colour_mode_from_int(2) == ColourMode::kNight);
  CHECK(colour_mode_from_int(3) == ColourMode::kDay);
  CHECK(colour_mode_from_int(-1) == ColourMode::kDay);
}
