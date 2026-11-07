// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Shared audio parameters, as the app's audio/AudioConfig.kt: 16 kHz mono
// PCM16, 20 ms frames = 320 samples = 640 bytes.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace audio {

constexpr int kSampleRate = 16000;
constexpr int kFrameMs = 20;
constexpr size_t kFrameSamples = kSampleRate * kFrameMs / 1000;
constexpr size_t kFrameBytes = kFrameSamples * 2;
// Hops a packet may travel through the relay mesh before it is dropped.
constexpr int kDefaultTtl = 4;

}  // namespace audio
}  // namespace crewradio
