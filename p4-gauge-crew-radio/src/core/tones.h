// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The key beeps (off unless switched on in the card, as on the phones): a
// short high tone when the mic keys, a lower one when it un-keys, and three
// low ones when the time-out cuts the mic. Played through the mixer's cue
// queue, over whatever is sounding. Each tone fades in and out over a few
// milliseconds, so it does not click.

#include <stdint.h>

#include <vector>

#include "audio_config.h"

namespace crewradio {
namespace tones {

enum class Cue : uint8_t { kKeyed, kUnkeyed, kTimedOut };

// The cue as whole 20 ms frames, kFrameSamples each, one after the other.
std::vector<int16_t> frames(Cue cue);

}  // namespace tones
}  // namespace crewradio
