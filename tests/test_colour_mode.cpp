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

using skgauges::colour_mode_from_sk_mode;
using skgauges::colour_mode_from_sk_sun;
using skgauges::SkColourMode;

TEST(colour_sk_mode_values_day_and_night_only) {
  ColourMode m = ColourMode::kDusk;
  CHECK(colour_mode_from_sk_mode("day", &m) && m == ColourMode::kDay);
  CHECK(colour_mode_from_sk_mode("night", &m) && m == ColourMode::kNight);
  m = ColourMode::kDusk;
  CHECK(!colour_mode_from_sk_mode("restricted visibility", &m));
  CHECK(!colour_mode_from_sk_mode("Night", &m));
  CHECK(!colour_mode_from_sk_mode("", &m));
  CHECK(!colour_mode_from_sk_mode(nullptr, &m));
  CHECK(m == ColourMode::kDusk);  // Untouched
}

TEST(colour_sk_sun_phases_by_how_dark_it_is) {
  struct Case {
    const char* phase;
    ColourMode mode;
  };
  const Case cases[] = {
      {"day", ColourMode::kDay},           {"sunrise", ColourMode::kDay},
      {"dawn", ColourMode::kDusk},         {"sunset", ColourMode::kDusk},
      {"nauticalDawn", ColourMode::kNight}, {"dusk", ColourMode::kNight},
      {"nauticalDusk", ColourMode::kNight}, {"night", ColourMode::kNight},
  };
  for (const Case& c : cases) {
    ColourMode m = ColourMode::kDay;
    CHECK(colour_mode_from_sk_sun(c.phase, &m) && m == c.mode);
  }
  ColourMode m = ColourMode::kDusk;
  CHECK(!colour_mode_from_sk_sun("noon", &m));
  CHECK(!colour_mode_from_sk_sun("sunriseEnd", &m));  // A time, not a phase
  CHECK(!colour_mode_from_sk_sun(nullptr, &m));
  CHECK(m == ColourMode::kDusk);
}

TEST(colour_sk_mode_alone_switches_on_each_change_only) {
  SkColourMode sk;
  ColourMode to = ColourMode::kDusk;
  CHECK(sk.on_mode("night", &to) && to == ColourMode::kNight);  // First value
  CHECK(!sk.on_mode("night", &to));    // Replayed on a reconnect: no switch
  CHECK(!sk.on_mode("restricted visibility", &to));
  CHECK(!sk.on_mode(nullptr, &to));     // A null from a lost source
  CHECK(sk.on_mode("day", &to) && to == ColourMode::kDay);
}

TEST(colour_sk_sun_decides_and_restating_it_never_undoes_a_tap) {
  SkColourMode sk;
  ColourMode to = ColourMode::kNight;
  CHECK(sk.on_sun("day", &to) && to == ColourMode::kDay);
  // Every minute the same phase again: no switch, so a mode chosen on the
  // unit's buttons holds.
  for (int i = 0; i < 5; i++) CHECK(!sk.on_sun("day", &to));
  CHECK(!sk.on_sun("sunrise", &to));  // Another phase, the same mode
  CHECK(sk.on_sun("sunset", &to) && to == ColourMode::kDusk);
  CHECK(sk.on_sun("dusk", &to) && to == ColourMode::kNight);
  CHECK(!sk.on_sun("nauticalDusk", &to));
  CHECK(!sk.on_sun("night", &to));
  CHECK(!sk.on_sun("nauticalDawn", &to));
  CHECK(sk.on_sun("dawn", &to) && to == ColourMode::kDusk);
  CHECK(sk.on_sun("sunrise", &to) && to == ColourMode::kDay);
}

TEST(colour_sk_derived_datas_own_mode_switch_follows_the_sun) {
  // At sunset derived-data sets environment.mode to night while the sun's
  // phase is sunset (dusk here): the sun decides, no jump to night and back.
  SkColourMode sk;
  ColourMode to = ColourMode::kDay;
  CHECK(sk.on_mode("day", &to) && to == ColourMode::kDay);
  CHECK(!sk.on_sun("day", &to));
  CHECK(sk.on_sun("sunset", &to) && to == ColourMode::kDusk);
  CHECK(!sk.on_mode("night", &to));
  CHECK(sk.on_sun("dusk", &to) && to == ColourMode::kNight);
  // Sunrise: day from both.
  CHECK(sk.on_sun("dawn", &to) && to == ColourMode::kDusk);
  CHECK(sk.on_sun("sunrise", &to) && to == ColourMode::kDay);
  CHECK(!sk.on_mode("day", &to));
}

TEST(colour_sk_mode_set_by_hand_against_the_sun_wins_until_its_next_step) {
  SkColourMode sk;
  ColourMode to = ColourMode::kNight;
  CHECK(sk.on_mode("day", &to) && to == ColourMode::kDay);
  CHECK(!sk.on_sun("day", &to));
  CHECK(sk.on_mode("night", &to) && to == ColourMode::kNight);  // App Dock
  CHECK(!sk.on_sun("day", &to));   // The sun restated: night holds
  CHECK(!sk.on_mode("night", &to));
  // Sunset: the sun's next step decides again.
  CHECK(sk.on_sun("sunset", &to) && to == ColourMode::kDusk);
  // Day by hand in the dark, until dawn.
  CHECK(sk.on_sun("night", &to) && to == ColourMode::kNight);
  CHECK(sk.on_mode("day", &to) && to == ColourMode::kDay);
  CHECK(!sk.on_sun("night", &to));
  // A phase with the same mode is no step: day holds through it.
  CHECK(!sk.on_sun("nauticalDawn", &to));
  CHECK(sk.on_sun("dawn", &to) && to == ColourMode::kDusk);
}

TEST(colour_sk_mode_left_at_night_does_not_hold_back_the_morning) {
  // derived-data leaves environment.mode at night until the sun is up, and
  // writes day in the same minute as the phase sunrise, or a minute later.
  SkColourMode sk;
  ColourMode to = ColourMode::kDay;
  CHECK(sk.on_mode("night", &to) && to == ColourMode::kNight);
  CHECK(!sk.on_sun("night", &to));
  CHECK(!sk.on_sun("nauticalDawn", &to));
  CHECK(sk.on_sun("dawn", &to) && to == ColourMode::kDusk);
  CHECK(sk.on_sun("sunrise", &to) && to == ColourMode::kDay);
  CHECK(!sk.on_mode("day", &to));
  // A first mode against the sun (at boot): who set it is unknown.
  SkColourMode boot;
  CHECK(boot.on_sun("day", &to) && to == ColourMode::kDay);
  CHECK(!boot.on_mode("night", &to));
}

TEST(colour_sk_first_values_agree_in_either_order) {
  // At boot (and on each reconnect) the server sends both cached values, in
  // no fixed order; the answer must not depend on it.
  const char* sets[][2] = {{"night", "sunset"}, {"night", "day"},
                           {"day", "night"},    {"day", "dawn"},
                           {"restricted visibility", "dusk"}};
  for (const auto& set : sets) {
    SkColourMode a;
    SkColourMode b;
    ColourMode ta = ColourMode::kDay;
    ColourMode tb = ColourMode::kDay;
    ColourMode last_a = ColourMode::kDay;
    ColourMode last_b = ColourMode::kDay;
    if (a.on_mode(set[0], &ta)) last_a = ta;
    if (a.on_sun(set[1], &ta)) last_a = ta;
    if (b.on_sun(set[1], &tb)) last_b = tb;
    if (b.on_mode(set[0], &tb)) last_b = tb;
    CHECK(last_a == last_b);
  }
}

TEST(colour_sk_seeded_answer_keeps_a_tap_across_a_restart) {
  // Night chosen on the buttons while Signal K says day, then a restart:
  // the server's first values answer day again, which is no change.
  SkColourMode before;
  ColourMode to = ColourMode::kNight;
  CHECK(before.on_sun("day", &to) && to == ColourMode::kDay);
  ColourMode saved = ColourMode::kNight;
  CHECK(before.answer(&saved) && saved == ColourMode::kDay);

  SkColourMode after;
  after.seed(saved);
  CHECK(!after.on_mode("day", &to));
  CHECK(!after.on_sun("day", &to));
  // The light changed while it was off: the first values switch.
  SkColourMode later;
  later.seed(ColourMode::kDay);
  CHECK(later.on_sun("dusk", &to) && to == ColourMode::kNight);
  // No answer saved: the first values switch, as before.
  SkColourMode fresh;
  CHECK(!fresh.answer(&saved));
  CHECK(fresh.on_sun("day", &to) && to == ColourMode::kDay);
}
