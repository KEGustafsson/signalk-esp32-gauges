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

// The ES7210 in TDM mode as the P4 reads it: four 16-bit slots a frame,
// [MIC1, MIC3, MIC2, MIC4] (its serial order; MIC3 is the board's echo
// reference, the ES8311's output), clocked as standard stereo I2S with
// 32-bit slots, so each 32-bit word holds two TDM slots, the first in its
// upper half. `words`: kFrameSamples pairs as read; `slots[s]`: TDM slot s.
constexpr int kTdmSlots = 4;
void unpack_tdm(const int32_t* words, int16_t slots[kTdmSlots][audio::kFrameSamples]);

// The mic the radio uses: channel 0 (MIC1) or 1 (MIC2) of `stereo`
// (kFrameSamples pairs), or kBoth: their average. A voice reaches the two
// alike and keeps its level; each one's own noise (the preamp's hiss) is
// independent of the other's and averages down, by up to 3 dB.
constexpr int kBoth = 2;
void take_channel(const int16_t* stereo, int channel,
                  int16_t mono[audio::kFrameSamples]);

// RMS of one frame, 0..32767.
int rms(const int16_t frame[audio::kFrameSamples]);

// One channel of an interleaved stereo frame, for the Status page's mic
// diagnostics: its mean (the DC a line stuck at one level shows: all ones
// is -1), its RMS about that mean (the signal), and its largest |sample|.
struct ChannelStats {
  int dc = 0;
  int ac_rms = 0;
  int peak = 0;
};
ChannelStats channel_stats(const int16_t* stereo, int channel);

// The ES7210 MICx_GAIN register's low nibble for `db`.
uint8_t es7210_gain_code(float db);

// The gain in dB that a code of es7210_gain_code's runs the PGA at.
float es7210_gain_db(uint8_t code);

}  // namespace mic
}  // namespace crewradio
