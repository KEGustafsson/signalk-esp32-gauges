// SPDX-License-Identifier: EUPL-1.2
#include "levels.h"

#include "mic.h"

namespace crewradio {
namespace levels {

namespace {

int clamp_volume(int v) {
  return v < 0 ? 0 : v > kVolumeMax ? kVolumeMax : v;
}

// The ES7210's PGA steps, lowest first (its codes 0-10, then 12, 13, 14).
constexpr float kGains[] = {0.0f,  3.0f,  6.0f,  9.0f,  12.0f,
                            15.0f, 18.0f, 21.0f, 24.0f, 27.0f,
                            30.0f, 34.5f, 36.0f, 37.5f};
constexpr int kGainCount = sizeof(kGains) / sizeof(kGains[0]);

}  // namespace

int step_volume(int volume, int dir) {
  int v = clamp_volume(volume);
  if (dir > 0) {
    v = (v / kVolumeStep + 1) * kVolumeStep;
  } else if (dir < 0) {
    v = ((v + kVolumeStep - 1) / kVolumeStep - 1) * kVolumeStep;
  }
  return clamp_volume(v);
}

float step_mic_gain(float db, int dir) {
  const float running = mic::es7210_gain_db(mic::es7210_gain_code(db));
  int i = 0;
  while (i < kGainCount - 1 && kGains[i] < running) i++;
  if (dir > 0 && i < kGainCount - 1) i++;
  if (dir < 0 && i > 0) i--;
  return kGains[i];
}

}  // namespace levels
}  // namespace crewradio
