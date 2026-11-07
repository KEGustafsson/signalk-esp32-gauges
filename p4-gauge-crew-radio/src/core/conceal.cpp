// SPDX-License-Identifier: EUPL-1.2
#include "conceal.h"

namespace crewradio {
namespace conceal {

bool frame(const int16_t* last, int n, int16_t out[audio::kFrameSamples]) {
  if (last == nullptr || n < 1 || n > kMaxFrames) return false;
  float gain = 1.0f;
  for (int i = 0; i < n; i++) gain *= kDecay;
  for (size_t i = 0; i < audio::kFrameSamples; i++) {
    // Truncated toward zero, as Kotlin's Float.toInt(): bit for bit the app.
    int v = (int)((float)last[i] * gain);
    out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
  }
  return true;
}

}  // namespace conceal
}  // namespace crewradio
