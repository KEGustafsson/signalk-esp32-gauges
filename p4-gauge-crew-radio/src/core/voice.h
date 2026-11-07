// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The mic's frame cleaned up before it goes to the crew: speexdsp's
// acoustic echo canceller (MDF) takes out what the unit's own speaker put
// into the mic, from the board's echo reference (the ES8311's output wired
// back to the ES7210's MIC3, captured in the same frame as the mic, so the
// two are sample-aligned), then its preprocessor takes out what the
// canceller left of the echo and, if asked, steady noise (an engine, a fan,
// the hiss of the preamp at a high gain).
//
// The canceller learns the speaker-to-mic path while the speaker plays,
// so it runs on every frame, talking or not: on a half-duplex unit it then
// takes out the key beeps; with the crew played while talking (full
// duplex) it is what keeps them from hearing themselves.
//
// Pure (speexdsp is plain C): host-tested in tests/test_voice.cpp.

#include <stdint.h>

#include "audio_config.h"

struct SpeexEchoState_;
struct SpeexPreprocessState_;

namespace crewradio {
namespace voice {

// The echo path the canceller models: the speaker and the mic in one
// housing, and the cabin's first reflections (MDF's filter, in frames).
// Measured on the 4" with its own speaker (2026-11-07): the direct path
// 0.1-0.2 ms, nearly all of the response within 5 ms; 20-60 ms tails
// cancel alike (about 26 dB, 29 with the residual suppression), 80 ms and
// more less (a longer filter learns slower), and cost more.
constexpr int kTailFrames = 2;  // 40 ms
// The noise suppression's range, dB of attenuation (0 = off).
constexpr int kMaxNoiseSuppressDb = 30;
constexpr int kDefaultNoiseSuppressDb = 15;

class Processor {
 public:
  // `tail_frames`: the echo path modelled, in frames (kTailFrames unless
  // measuring).
  explicit Processor(int tail_frames = kTailFrames);
  ~Processor();
  Processor(const Processor&) = delete;
  Processor& operator=(const Processor&) = delete;

  // False when speexdsp's states could not be allocated: process() then
  // copies the mic through.
  bool ok() const { return echo_ != nullptr && pre_ != nullptr; }

  // What to do from the next frame on. `noise_db` 0..kMaxNoiseSuppressDb
  // (clamped). Turning the canceller back on starts it learning afresh.
  void set(bool echo_cancel, int noise_db);
  bool echo_cancel() const { return echo_on_; }
  int noise_db() const { return noise_db_; }

  // One frame: `mic` as heard, `ref` what the speaker played in the same
  // frame, `out` what goes to the crew (may be `mic` itself). Whenever the
  // preprocessor runs (the canceller or the noise suppression on), `out`
  // is a frame late: it works in overlapping windows.
  void process(const int16_t mic[audio::kFrameSamples],
               const int16_t ref[audio::kFrameSamples],
               int16_t out[audio::kFrameSamples]);

  // How much the canceller took out (dB, the mic's energy over what it
  // left), over the frames since the last call in which the speaker played
  // something; false (and `db` untouched) when it played nothing, or the
  // canceller is off.
  bool take_echo_db(int* db);

 private:
  void apply();

  SpeexEchoState_* echo_ = nullptr;
  SpeexPreprocessState_* pre_ = nullptr;
  bool echo_on_ = true;
  int noise_db_ = kDefaultNoiseSuppressDb;
  double mic_energy_ = 0.0;   // Frames with the speaker playing, since
  double left_energy_ = 0.0;  // the last take_echo_db()
  int frames_ = 0;
};

}  // namespace voice
}  // namespace crewradio
