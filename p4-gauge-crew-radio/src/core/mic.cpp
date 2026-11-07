// SPDX-License-Identifier: EUPL-1.2
#include "mic.h"

#include <math.h>

namespace crewradio {
namespace mic {

void unpack_tdm(const int32_t* words,
                int16_t slots[kTdmSlots][audio::kFrameSamples]) {
  for (size_t i = 0; i < audio::kFrameSamples; i++) {
    const uint32_t left = (uint32_t)words[2 * i];
    const uint32_t right = (uint32_t)words[2 * i + 1];
    slots[0][i] = (int16_t)(uint16_t)(left >> 16);
    slots[1][i] = (int16_t)(uint16_t)(left & 0xFFFF);
    slots[2][i] = (int16_t)(uint16_t)(right >> 16);
    slots[3][i] = (int16_t)(uint16_t)(right & 0xFFFF);
  }
}

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

ChannelStats channel_stats(const int16_t* stereo, int channel) {
  const int c = channel == 1 ? 1 : 0;
  int64_t sum = 0;
  int peak = 0;
  for (size_t i = 0; i < audio::kFrameSamples; i++) {
    const int v = stereo[2 * i + c];
    sum += v;
    const int a = v < 0 ? -v : v;
    if (a > peak) peak = a;
  }
  const double mean = (double)sum / audio::kFrameSamples;
  double var = 0.0;
  for (size_t i = 0; i < audio::kFrameSamples; i++) {
    const double d = stereo[2 * i + c] - mean;
    var += d * d;
  }
  ChannelStats s;
  s.dc = (int)lrint(mean);
  s.ac_rms = (int)lrint(sqrt(var / audio::kFrameSamples));
  s.peak = peak;
  return s;
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

float es7210_gain_db(uint8_t code) {
  if (code <= 10) return 3.0f * code;
  if (code <= 12) return 34.5f;  // 11 is not a step es7210_gain_code gives
  if (code == 13) return 36.0f;
  return 37.5f;
}

}  // namespace mic
}  // namespace crewradio
