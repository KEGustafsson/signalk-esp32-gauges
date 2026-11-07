// SPDX-License-Identifier: EUPL-1.2
// The screen's level steps (the Crew page's - and +): the volume onto steps
// of 5, the mic gain one ES7210 PGA step at a time.
#include "core/levels.h"
#include "core/mic.h"
#include "test.h"

using namespace crewradio;

TEST(levels_volume_steps_onto_multiples_of_five) {
  CHECK(levels::step_volume(70, 1) == 75);
  CHECK(levels::step_volume(70, -1) == 65);
  // A card's own value between two steps goes to the next one each way.
  CHECK(levels::step_volume(72, 1) == 75);
  CHECK(levels::step_volume(72, -1) == 70);
  CHECK(levels::step_volume(72, 0) == 72);
}

TEST(levels_volume_stops_at_mute_and_full) {
  CHECK(levels::step_volume(5, -1) == 0);
  CHECK(levels::step_volume(3, -1) == 0);
  CHECK(levels::step_volume(0, -1) == 0);  // Mute stays mute
  CHECK(levels::step_volume(0, 1) == 5);
  CHECK(levels::step_volume(98, 1) == 100);
  CHECK(levels::step_volume(100, 1) == 100);
  // Out of range (a hand-edited card file) is clamped first.
  CHECK(levels::step_volume(250, -1) == 95);
  CHECK(levels::step_volume(-20, 1) == 5);
}

TEST(levels_volume_walks_every_step_both_ways) {
  int v = 0;
  for (int i = 0; i < 20; i++) v = levels::step_volume(v, 1);
  CHECK(v == 100);
  for (int i = 0; i < 20; i++) v = levels::step_volume(v, -1);
  CHECK(v == 0);
}

TEST(levels_mic_gain_takes_the_pga_steps) {
  CHECK_NEAR(levels::step_mic_gain(30.0f, 1), 34.5, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(34.5f, 1), 36.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(36.0f, 1), 37.5, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(37.5f, 1), 37.5, 1e-6);  // The top
  CHECK_NEAR(levels::step_mic_gain(30.0f, -1), 27.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(3.0f, -1), 0.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(0.0f, -1), 0.0, 1e-6);  // The bottom
  CHECK_NEAR(levels::step_mic_gain(34.5f, -1), 30.0, 1e-6);
}

TEST(levels_mic_gain_steps_from_what_the_codec_runs_at) {
  // 32 and 33 dB run at 30 (es7210_gain_code), so a step is from 30.
  CHECK_NEAR(levels::step_mic_gain(32.0f, 0), 30.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(32.0f, 1), 34.5, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(33.0f, -1), 27.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(-4.0f, 1), 3.0, 1e-6);
  CHECK_NEAR(levels::step_mic_gain(99.0f, -1), 36.0, 1e-6);
}

TEST(levels_every_mic_step_is_one_the_codec_keeps) {
  // Walking up the whole range: each step is a PGA code's own gain, and
  // es7210_gain_code gives that code back for it.
  float db = 0.0f;
  int steps = 0;
  for (int i = 0; i < 20; i++) {
    const float next = levels::step_mic_gain(db, 1);
    if (next == db) break;
    const uint8_t code = mic::es7210_gain_code(next);
    CHECK_NEAR(mic::es7210_gain_db(code), next, 1e-6);
    CHECK(next > db);
    db = next;
    steps++;
  }
  CHECK(steps == 13);  // 14 gains, 0 to 37.5
  CHECK_NEAR(db, 37.5, 1e-6);
}
