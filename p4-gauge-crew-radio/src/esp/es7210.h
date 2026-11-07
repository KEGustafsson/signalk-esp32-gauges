// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The 4"'s ES7210 microphone ADC: all four inputs in TDM, 16-bit slots at
// 16 kHz (MIC1, MIC3, MIC2, MIC4; MIC1/MIC2 are the capsules, MIC3 the
// ES8311's output as an echo reference), slave to the P4's I2S (MCLK
// 256 fs, a 64 fs bit clock, shared with the ES8311), over I2C port 0
// through the legacy driver, as the ES8311 (es8311.h).
//
// The register values follow Espressif's esp_codec_dev ES7210 driver
// (Apache-2.0): open in slave mode, MIC1-4, 16-bit TDM, start; as
// Waveshare's firmware for this board opens it.

#if defined(ESP_PLATFORM)

#include <stdint.h>

namespace crewradio {
namespace esp {

// Brings up MIC1 and MIC2 with `gain_db` of PGA gain (0-37.5), MIC3 (the
// echo reference) at 0 dB, in TDM. False if no
// ES7210 answered at 0x40 or 0x41.
bool es7210_start(float gain_db);
// MIC1's and MIC2's PGA to `gain_db` while running (after es7210_start).
bool es7210_set_gain(float gain_db);
// The chip's main registers as read back, one log line.
void es7210_log_registers();
void es7210_stop();

}  // namespace esp
}  // namespace crewradio

#endif
