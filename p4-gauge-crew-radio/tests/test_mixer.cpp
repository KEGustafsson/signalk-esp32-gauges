// SPDX-License-Identifier: EUPL-1.2
// Concealment and the mixer, as the app's ConcealTest and MixerTest.
#include "core/conceal.h"
#include "core/mixer.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr size_t N = audio::kFrameSamples;

struct Frame {
  int16_t s[N];
  explicit Frame(int16_t v = 0) {
    for (size_t i = 0; i < N; i++) s[i] = v;
  }
};

int16_t tick(Mixer& m, int64_t t, int* active = nullptr) {
  Frame out;
  const int a = m.tick(t, out.s);
  if (active) *active = a;
  return out.s[0];
}

}  // namespace

TEST(conceal_repeats_the_last_frame_with_decaying_gain) {
  Frame last(10000), out;
  CHECK(conceal::frame(last.s, 1, out.s) && out.s[0] == 6000);
  CHECK(conceal::frame(last.s, 2, out.s) && out.s[0] == 3600);
  CHECK(conceal::frame(last.s, 3, out.s) && out.s[0] == 2160);
  CHECK(!conceal::frame(last.s, 4, out.s));
  CHECK(!conceal::frame(nullptr, 1, out.s));
  Frame neg(-10001);
  CHECK(conceal::frame(neg.s, 1, out.s) && out.s[0] == -6000);  // Toward zero
}

TEST(mixer_a_stream_waits_for_two_frames) {
  Mixer m;
  m.push(1, Frame(100).s, 0);
  CHECK(tick(m, 0) == 0);
  m.push(1, Frame(200).s, 20);
  CHECK(tick(m, 20) == 100);
  CHECK(tick(m, 40) == 200);
}

TEST(mixer_a_hole_is_concealed_and_real_audio_resumes) {
  Mixer m;
  m.push(1, Frame(10000).s, 0);
  m.push(1, Frame(10000).s, 0);
  m.conceal(1, 1);
  m.push(1, Frame(500).s, 0);
  CHECK(tick(m, 0) == 10000);
  CHECK(tick(m, 20) == 10000);
  CHECK(tick(m, 40) == 6000);  // The hole
  CHECK(tick(m, 60) == 500);
  CHECK(m.concealed_frames() == 1);
}

TEST(mixer_concealment_gives_up_after_three_and_reserves_no_more) {
  Mixer m;
  m.push(1, Frame(10000).s, 0);
  m.push(1, Frame(10000).s, 0);
  m.conceal(1, 9);
  tick(m, 0);
  tick(m, 0);
  CHECK(tick(m, 0) == 6000);
  CHECK(tick(m, 0) == 3600);
  CHECK(tick(m, 0) == 2160);
  CHECK(tick(m, 0) == 0);
  CHECK(m.concealed_frames() == 3);
}

TEST(mixer_a_queue_that_runs_dry_mid_talk_is_concealed) {
  Mixer m;
  m.push(1, Frame(10000).s, 0);
  m.push(1, Frame(10000).s, 0);
  tick(m, 0);
  tick(m, 20);
  CHECK(tick(m, 40) == 6000);  // Dry, but heard 40 ms ago: still talking
  CHECK(tick(m, 200) == 0);    // 200 ms quiet: the burst is over
}

TEST(mixer_drops_the_oldest_beyond_ten) {
  Mixer m;
  for (int i = 0; i < 12; i++) m.push(1, Frame((int16_t)(i + 1)).s, 0);
  CHECK(tick(m, 0) == 3);
}

TEST(mixer_gain_scales_speech_not_the_cue_and_mute_silences_all) {
  Mixer m;
  m.set_gain(0.5f);
  m.push(1, Frame(1000).s, 0);
  m.push(1, Frame(1000).s, 0);
  m.cue(Frame(100).s);
  CHECK(tick(m, 0) == 600);
  m.set_muted(true);
  m.cue(Frame(100).s);
  CHECK(tick(m, 0) == 0);
}

TEST(mixer_two_talkers_share_the_headroom_and_loud_ones_clip) {
  Mixer m;
  for (int id = 1; id <= 2; id++) {
    m.push(id, Frame(10000).s, 0);
    m.push(id, Frame(30000).s, 0);
  }
  int active = 0;
  // 20000 x round(32768 / sqrt 2) >> 15, floored as in the app.
  CHECK(tick(m, 0, &active) == 14141 && active == 2);
  CHECK(tick(m, 0) == 32767);  // 60000 / sqrt 2 = 42426: clipped, not wrapped
}

TEST(mixer_an_idle_stream_is_swept_after_a_second) {
  Mixer m;
  m.push(1, Frame(1).s, 0);
  CHECK(m.stream_count() == 1);
  tick(m, 500);
  CHECK(m.stream_count() == 1);
  tick(m, 1001);
  CHECK(m.stream_count() == 0);
}

TEST(mixer_a_stream_that_paused_prefills_again) {
  Mixer m;
  m.push(1, Frame(7).s, 0);
  m.push(1, Frame(7).s, 0);
  tick(m, 0);
  tick(m, 20);
  tick(m, 400);  // Rested
  m.push(1, Frame(9).s, 500);
  CHECK(tick(m, 500) == 0);  // Waits for a second frame again
  m.push(1, Frame(9).s, 520);
  CHECK(tick(m, 520) == 9);
}

TEST(mixer_a_gap_against_a_stream_at_rest_reserves_nothing) {
  Mixer m;
  m.push(1, Frame(7).s, 0);
  m.push(1, Frame(7).s, 0);
  tick(m, 0);
  tick(m, 20);
  tick(m, 400);  // Rested, nothing queued
  m.conceal(1, 3);
  m.push(1, Frame(9).s, 500);
  m.push(1, Frame(9).s, 520);
  CHECK(tick(m, 520) == 9);  // Not a hole first
}

TEST(mixer_more_talkers_than_streams_are_refused_and_reset_clears) {
  Mixer m;
  for (int id = 0; id < Mixer::kMaxStreams; id++) CHECK(m.push(id, Frame(1).s, 0));
  CHECK(!m.push(99, Frame(1).s, 0));
  m.reset();
  CHECK(m.stream_count() == 0);
  CHECK(m.push(99, Frame(1).s, 0));
}
