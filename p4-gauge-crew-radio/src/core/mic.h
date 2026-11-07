// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The microphone side, pure: one channel out of the ES7210's interleaved
// stereo I2S frame (MIC1 left, MIC2 right), its level for the status page,
// and the ES7210's PGA code for a gain in dB, rounded as esp_codec_dev
// rounds it: 3 dB steps to 30 dB, then 34.5, 36 and 37.5 (33 dB rounds to
// 30 there, and so here).

#include <stddef.h>
#include <stdint.h>

#include "audio_config.h"

namespace crewradio {
namespace mic {

constexpr float kMaxGainDb = 37.5f;

// channel 0 or 1 of `stereo` (kFrameSamples pairs) into `mono`.
void take_channel(const int16_t* stereo, int channel,
                  int16_t mono[audio::kFrameSamples]);

// RMS of one frame, 0..32767.
int rms(const int16_t frame[audio::kFrameSamples]);

// The ES7210 MICx_GAIN register's low nibble for `db`.
uint8_t es7210_gain_code(float db);

}  // namespace mic
}  // namespace crewradio
