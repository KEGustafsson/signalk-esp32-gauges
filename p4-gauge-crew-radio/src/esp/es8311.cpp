// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "es8311.h"

#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace crewradio {
namespace esp {

namespace {

constexpr const char* kTag = "es8311";
constexpr i2c_port_t kPort = I2C_NUM_0;
constexpr TickType_t kTimeout = pdMS_TO_TICKS(50);

bool write_reg(uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return i2c_master_write_to_device(kPort, kEs8311Address, buf, sizeof(buf),
                                    kTimeout) == ESP_OK;
}

bool read_reg(uint8_t reg, uint8_t* value) {
  return i2c_master_write_read_device(kPort, kEs8311Address, &reg, 1, value, 1,
                                      kTimeout) == ESP_OK;
}

bool update_reg(uint8_t reg, uint8_t keep_mask, uint8_t set) {
  uint8_t v = 0;
  if (!read_reg(reg, &v)) return false;
  return write_reg(reg, (uint8_t)((v & keep_mask) | set));
}

}  // namespace

bool es8311_start() {
  uint8_t id1 = 0, id2 = 0;
  if (!read_reg(0xFD, &id1) || !read_reg(0xFE, &id2)) {
    ESP_LOGE(kTag, "No ES8311 at 0x%02x on I2C port 0", kEs8311Address);
    return false;
  }
  ESP_LOGI(kTag, "ES8311 chip id %02x%02x", id1, id2);
  bool ok = true;
  // Open (esp_codec_dev es8311_open, slave, MCLK from the pin, not inverted).
  ok &= write_reg(0x0D, 0xFA);
  ok &= write_reg(0x44, 0x08);  // I2C noise immunity; written twice, as the
  ok &= write_reg(0x44, 0x08);  // first write is known to fail at times
  ok &= write_reg(0x01, 0x30);
  ok &= write_reg(0x02, 0x00);
  ok &= write_reg(0x03, 0x10);
  ok &= write_reg(0x16, 0x24);
  ok &= write_reg(0x04, 0x10);
  ok &= write_reg(0x05, 0x00);
  ok &= write_reg(0x0B, 0x00);
  ok &= write_reg(0x0C, 0x00);
  ok &= write_reg(0x10, 0x1F);
  ok &= write_reg(0x11, 0x7F);
  ok &= write_reg(0x00, 0x80);   // Power on, slave (bit 6 clear)
  ok &= write_reg(0x01, 0x3F);   // Clocks on, MCLK from the pin
  ok &= update_reg(0x06, 0xDF, 0x00);  // SCLK not inverted
  ok &= write_reg(0x13, 0x10);
  ok &= write_reg(0x1B, 0x0A);
  ok &= write_reg(0x1C, 0x6A);
  ok &= write_reg(0x44, 0x58);   // DAC reference to the ADC (for later AEC)
  // 16 kHz from MCLK 4.096 MHz: pre_div 1, x1, adc/dac div 1, single speed,
  // adc osr 0x10, dac osr 0x20 (lrck and bclk dividers matter only to a
  // master).
  ok &= update_reg(0x02, 0x07, 0x00);
  ok &= write_reg(0x05, 0x00);
  ok &= update_reg(0x03, 0x80, 0x10);
  ok &= update_reg(0x04, 0x80, 0x20);
  ok &= update_reg(0x07, 0xC0, 0x00);
  ok &= write_reg(0x08, 0xFF);
  ok &= update_reg(0x06, 0xE0, 0x03);
  // I2S (Philips), 16-bit, both ports, then unmuted (bit 6 clear).
  ok &= update_reg(0x09, 0xBC, 0x0C);
  ok &= update_reg(0x0A, 0xBC, 0x0C);
  // Start (es8311_start, DAC).
  ok &= write_reg(0x17, 0xBF);
  ok &= write_reg(0x0E, 0x02);
  ok &= write_reg(0x12, 0x00);   // DAC on
  ok &= write_reg(0x14, 0x1A);   // Analog mic, no DMIC (bit 6 clear)
  ok &= write_reg(0x0D, 0x01);
  ok &= write_reg(0x15, 0x40);
  ok &= write_reg(0x37, 0x08);
  ok &= write_reg(0x45, 0x00);
  ok &= update_reg(0x31, 0x9F, 0x00);  // DAC not muted
  if (!ok) ESP_LOGE(kTag, "ES8311 did not take its settings");
  return ok;
}

bool es8311_set_volume(int percent) {
  if (percent <= 0) {
    return update_reg(0x31, 0x9F, 0x60) && write_reg(0x32, 0x00);
  }
  if (percent > 100) percent = 100;
  // 0xBF is 0 dB, 0.5 dB a step: 100 -> 0 dB, 1 -> -49.5 dB.
  return update_reg(0x31, 0x9F, 0x00) &&
         write_reg(0x32, (uint8_t)(0xBF - (100 - percent)));
}

void es8311_stop() {
  write_reg(0x32, 0x00);
  write_reg(0x17, 0x00);
  write_reg(0x0E, 0xFF);
  write_reg(0x12, 0x02);
  write_reg(0x14, 0x00);
  write_reg(0x0D, 0xFA);
}

}  // namespace esp
}  // namespace crewradio
#endif
