// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The 4"'s ES7210 microphone ADC: MIC1 and MIC2 as 16-bit I2S stereo at
// 16 kHz, slave to the P4's I2S (MCLK 256 fs, shared with the ES8311), over
// I2C port 0 through the legacy driver, as the ES8311 (es8311.h).
//
// The register values follow Espressif's esp_codec_dev ES7210 driver
// (Apache-2.0): open in slave mode, MIC1 + MIC2, 16-bit I2S, start.

#if defined(ESP_PLATFORM)

#include <stdint.h>

namespace crewradio {
namespace esp {

// Brings up MIC1 and MIC2 with `gain_db` of PGA gain (0-37.5). False if no
// ES7210 answered at 0x40 or 0x41.
bool es7210_start(float gain_db);
void es7210_stop();

}  // namespace esp
}  // namespace crewradio

#endif
