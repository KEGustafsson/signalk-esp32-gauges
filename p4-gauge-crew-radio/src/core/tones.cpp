// SPDX-License-Identifier: EUPL-1.2
#include "tones.h"

#include <math.h>

namespace crewradio {
namespace tones {

namespace {

constexpr float kLevel = 6000.0f;  // About -15 dBFS: heard, not startling
constexpr int kFadeSamples = 64;   // 4 ms

// A tone of `ms` milliseconds at `hz` appended to `out`.
void tone(std::vector<int16_t>* out, float hz, int ms) {
  const int n = audio::kSampleRate * ms / 1000;
  for (int i = 0; i < n; i++) {
    float env = 1.0f;
    if (i < kFadeSamples) env = (float)i / kFadeSamples;
    if (n - 1 - i < kFadeSamples) env = (float)(n - 1 - i) / kFadeSamples;
    const float v = kLevel * env *
                    sinf(2.0f * 3.14159265f * hz * (float)i / audio::kSampleRate);
    out->push_back((int16_t)lrintf(v));
  }
}

void gap(std::vector<int16_t>* out, int ms) {
  out->insert(out->end(), (size_t)(audio::kSampleRate * ms / 1000), 0);
}

}  // namespace

std::vector<int16_t> frames(Cue cue) {
  std::vector<int16_t> out;
  switch (cue) {
    case Cue::kKeyed:
      tone(&out, 1200.0f, 60);
      break;
    case Cue::kUnkeyed:
      tone(&out, 700.0f, 60);
      break;
    case Cue::kTimedOut:
      for (int i = 0; i < 3; i++) {
        tone(&out, 450.0f, 80);
        gap(&out, 60);
      }
      break;
  }
  // Pad to whole frames.
  const size_t rem = out.size() % audio::kFrameSamples;
  if (rem != 0) out.insert(out.end(), audio::kFrameSamples - rem, 0);
  return out;
}

}  // namespace tones
}  // namespace crewradio
