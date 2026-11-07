// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The 4"'s ES8311 codec (DAC side, to the NS4150B speaker amplifier), at
// 16 kHz, 16-bit I2S, slave to the P4's I2S with MCLK = 256 fs. Its
// registers are set over I2C port 0, the bus the touch controller is on:
// through ESP-IDF's legacy I2C driver, the one ESP32_Display_Panel installs
// on that port (linking the new driver beside it aborts at boot), which
// locks the bus per transfer.
//
// The register values follow Espressif's esp_codec_dev ES8311 driver
// (Apache-2.0; open, 16 kHz / 4.096 MHz coefficients, I2S format, start).

#if defined(ESP_PLATFORM)

#include <stdint.h>

namespace crewradio {
namespace esp {

constexpr uint8_t kEs8311Address = 0x18;

// Powers up the DAC path for 16 kHz playback. False if the codec did not
// answer.
bool es8311_start();
// 0..100: 0 mutes the DAC, 100 is 0 dB, each step below is 0.5 dB.
bool es8311_set_volume(int percent);
// Powers the DAC path down.
void es8311_stop();

}  // namespace esp
}  // namespace crewradio

#endif
