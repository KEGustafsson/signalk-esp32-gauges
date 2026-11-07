// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The speaker volume and the mic gain as the gauge's screen steps them (the
// Crew page's - and +, one step a tap), onto values the codecs take: the
// card's 0-100 volume in steps of 5 (2.5 dB at the ES8311's 0.5 dB a
// step), the ES7210's PGA one of its own steps at a time (core/mic.h).

namespace crewradio {
namespace levels {

constexpr int kVolumeMax = 100;
constexpr int kVolumeStep = 5;

// `volume` one step up (dir > 0) or down (dir < 0), onto the multiples of
// kVolumeStep: 72 goes to 75 or to 70. Clamped to 0-100 (0 is mute); dir 0
// leaves it as it is (clamped).
int step_volume(int volume, int dir);

// The gain the ES7210 runs at for a card's `db` (what es7210_gain_code
// rounds it to), one PGA step up or down: 0, 3, ..., 30, 34.5, 36, 37.5.
// A card's 32 dB runs at 30, so it steps to 34.5 or to 27. Clamped to the
// ends; dir 0 gives the gain it runs at.
float step_mic_gain(float db, int dir);

}  // namespace levels
}  // namespace crewradio
