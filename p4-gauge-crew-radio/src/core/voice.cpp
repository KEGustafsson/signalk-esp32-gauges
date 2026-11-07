// SPDX-License-Identifier: EUPL-1.2
#include "voice.h"

#include <math.h>
#include <string.h>

#include "../third_party/speexdsp/speex/speex_echo.h"
#include "../third_party/speexdsp/speex/speex_preprocess.h"

namespace crewradio {
namespace voice {

namespace {

constexpr int kN = (int)audio::kFrameSamples;
// A reference frame quieter than this (RMS) is the speaker playing nothing:
// not counted in take_echo_db().
constexpr int64_t kRefActiveEnergy = 20 * 20 * (int64_t)kN;  // RMS 20
// What the preprocessor takes out of the canceller's residue (speexdsp's
// defaults: -40 dB, -15 dB while the near end talks too).
constexpr spx_int32_t kEchoSuppressDb = -40;
constexpr spx_int32_t kEchoSuppressActiveDb = -15;

// In integers: the P4's FPU has no double precision, and a double sum
// over a frame would be hundreds of soft-float calls a frame.
int64_t energy(const int16_t* f) {
  int64_t e = 0;
  for (int i = 0; i < kN; i++) e += (int32_t)f[i] * f[i];
  return e;
}

}  // namespace

Processor::Processor(int tail_frames) {
  if (tail_frames < 1) tail_frames = 1;
  echo_ = speex_echo_state_init(kN, kN * tail_frames);
  pre_ = speex_preprocess_state_init(kN, audio::kSampleRate);
  if (echo_ != nullptr) {
    spx_int32_t rate = audio::kSampleRate;
    speex_echo_ctl(echo_, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
  }
  if (pre_ != nullptr) {
    spx_int32_t on = 1, off = 0;
    spx_int32_t echo_db = kEchoSuppressDb, active_db = kEchoSuppressActiveDb;
    // The preprocessor's gain is also what suppresses the residual echo,
    // so it stays on; the noise floor alone is the setting (0 dB: none).
    // Its VAD is off by default (and setting it prints a warning).
    speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_DENOISE, &on);
    speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_AGC, &off);
    speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_DEREVERB, &off);
    speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS, &echo_db);
    speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_ECHO_SUPPRESS_ACTIVE,
                         &active_db);
  }
  apply();
}

Processor::~Processor() {
  if (pre_ != nullptr) speex_preprocess_state_destroy(pre_);
  if (echo_ != nullptr) speex_echo_state_destroy(echo_);
}

void Processor::set(bool echo_cancel, int noise_db) {
  if (noise_db < 0) noise_db = 0;
  if (noise_db > kMaxNoiseSuppressDb) noise_db = kMaxNoiseSuppressDb;
  if (echo_cancel && !echo_on_ && echo_ != nullptr) {
    speex_echo_state_reset(echo_);  // What it learned may be long out of date
  }
  echo_on_ = echo_cancel;
  noise_db_ = noise_db;
  apply();
}

void Processor::apply() {
  if (pre_ == nullptr) return;
  spx_int32_t floor_db = -noise_db_;
  speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_NOISE_SUPPRESS, &floor_db);
  // The canceller's residue goes to the preprocessor only while it runs.
  speex_preprocess_ctl(pre_, SPEEX_PREPROCESS_SET_ECHO_STATE,
                       echo_on_ && echo_ != nullptr ? echo_ : nullptr);
}

void Processor::process(const int16_t mic[audio::kFrameSamples],
                        const int16_t ref[audio::kFrameSamples],
                        int16_t out[audio::kFrameSamples]) {
  if (!ok()) {
    if (out != mic) memcpy(out, mic, sizeof(int16_t) * kN);
    return;
  }
  int16_t left[kN];
  if (echo_on_) {
    speex_echo_cancellation(echo_, mic, ref, left);
    if (energy(ref) > kRefActiveEnergy) {
      mic_energy_ += (double)energy(mic);
      left_energy_ += (double)energy(left);
      frames_++;
    }
  } else {
    memcpy(left, mic, sizeof(left));
  }
  if (echo_on_ || noise_db_ > 0) speex_preprocess_run(pre_, left);
  memcpy(out, left, sizeof(left));
}

bool Processor::take_echo_db(int* db) {
  const bool any = echo_on_ && frames_ > 0;
  if (any) {
    // At least 1 LSB of residue a sample: a perfectly silent residue is not
    // an infinite figure.
    const double left = left_energy_ > frames_ * kN ? left_energy_ : frames_ * kN;
    *db = (int)lround(10.0 * log10((mic_energy_ + 1.0) / left));
  }
  mic_energy_ = left_energy_ = 0.0;
  frames_ = 0;
  return any;
}

}  // namespace voice
}  // namespace crewradio
