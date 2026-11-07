// SPDX-License-Identifier: EUPL-1.2
#include "mic.h"

#include <math.h>

namespace crewradio {
namespace mic {

void take_channel(const int16_t* stereo, int channel,
                  int16_t mono[audio::kFrameSamples]) {
  const int c = channel == 1 ? 1 : 0;
  for (size_t i = 0; i < audio::kFrameSamples; i++) mono[i] = stereo[2 * i + c];
}

int rms(const int16_t frame[audio::kFrameSamples]) {
  int64_t sum = 0;
  for (size_t i = 0; i < audio::kFrameSamples; i++) {
    sum += (int64_t)frame[i] * frame[i];
  }
  return (int)lrint(sqrt((double)sum / audio::kFrameSamples));
}

uint8_t es7210_gain_code(float db) {
  if (!(db >= 0.0f)) db = 0.0f;  // NaN too
  db += 0.5f;
  if (db < 33.0f) return (uint8_t)(db < 3.0f ? 0 : (int)(db / 3.0f));
  if (db < 34.5f) return 10;  // 30 dB
  if (db < 36.0f) return 12;  // 34.5 dB
  if (db < 37.0f) return 13;  // 36 dB
  return 14;                  // 37.5 dB
}

}  // namespace mic
}  // namespace crewradio
