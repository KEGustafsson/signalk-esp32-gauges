// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Packet-loss concealment for one 20 ms slot, as the app's audio/Conceal.kt:
// the sender's last good frame, repeated with gain 0.6^n; past three slots a
// repeat sounds like a stutter, so it gives way to silence. Codec-agnostic,
// so PCM senders are covered too.

#include <stdint.h>

#include "audio_config.h"

namespace crewradio {
namespace conceal {

constexpr int kMaxFrames = 3;
constexpr float kDecay = 0.6f;

// Writes the frame to play in place of the n-th consecutive missing one
// (1-based) from `last`; false once the gap is too long, or with nothing to
// repeat (last == nullptr).
bool frame(const int16_t* last, int n, int16_t out[audio::kFrameSamples]);

}  // namespace conceal
}  // namespace crewradio
