// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "log_level.h"  // First: INFO and WARN not compiled out
#include "es7210.h"

#include <driver/i2c.h>
#include <esp_log.h>
#include <stdio.h>

#include "core/mic.h"

namespace crewradio {
namespace esp {

namespace {

constexpr const char* kTag = "es7210";
constexpr i2c_port_t kPort = I2C_NUM_0;
constexpr TickType_t kTimeout = pdMS_TO_TICKS(50);
uint8_t g_address = 0x40;
constexpr uint8_t kMicPower = 0x08;  // Registers 0x47-0x4A, as esp_codec_dev

bool write_reg(uint8_t reg, uint8_t value) {
  const uint8_t buf[2] = {reg, value};
  return i2c_master_write_to_device(kPort, g_address, buf, sizeof(buf),
                                    kTimeout) == ESP_OK;
}

bool read_reg(uint8_t reg, uint8_t* value) {
  return i2c_master_write_read_device(kPort, g_address, &reg, 1, value, 1,
                                      kTimeout) == ESP_OK;
}

bool update_reg(uint8_t reg, uint8_t mask, uint8_t value) {
  uint8_t v = 0;
  if (!read_reg(reg, &v)) return false;
  return write_reg(reg, (uint8_t)((v & ~mask) | (value & mask)));
}

// All four inputs on, as Waveshare's firmware for this board opens it
// (esp_codec_dev's es7210_mic_select with MIC1-4): MIC1 and MIC2 (the
// capsules) at `code`, MIC3 (the ES8311's output, the echo reference, at
// line level) at 0 dB, MIC4 (not connected) at 0 dB; TDM out, so all four
// reach the one SDOUT1 line the P4 reads.
bool select_mics(uint8_t code) {
  bool ok = true;
  for (uint8_t r = 0x43; r <= 0x46; r++) ok &= update_reg(r, 0x10, 0x00);
  ok &= write_reg(0x4B, 0xFF);
  ok &= write_reg(0x4C, 0xFF);
  ok &= update_reg(0x01, 0x0B, 0x00);  // MIC1/2 clocks on
  ok &= update_reg(0x01, 0x15, 0x00);  // MIC3/4 clocks on
  ok &= write_reg(0x4B, 0x00);         // MIC1/2 bias, ADC and PGA powered
  ok &= write_reg(0x4C, 0x00);         // MIC3/4 the same
  for (uint8_t r = 0x43; r <= 0x46; r++) {
    ok &= update_reg(r, 0x10, 0x10);
    ok &= update_reg(r, 0x0F, r <= 0x44 ? code : 0);
  }
  ok &= write_reg(0x12, 0x02);  // TDM: MIC1, MIC3, MIC2, MIC4 on SDOUT1
  return ok;
}

}  // namespace

bool es7210_start(float gain_db) {
  uint8_t probe = 0;
  g_address = 0x40;
  if (!read_reg(0x00, &probe)) {
    g_address = 0x41;
    if (!read_reg(0x00, &probe)) {
      ESP_LOGE(kTag, "No ES7210 at 0x40 or 0x41 on I2C port 0");
      return false;
    }
  }
  const uint8_t code = mic::es7210_gain_code(gain_db);
  bool ok = true;
  // Open (slave).
  ok &= write_reg(0x00, 0xFF);
  ok &= write_reg(0x00, 0x41);
  ok &= write_reg(0x01, 0x3F);
  ok &= write_reg(0x09, 0x30);  // Chip state cycle
  ok &= write_reg(0x0A, 0x30);  // Power-on state cycle
  ok &= write_reg(0x23, 0x2A);  // High-pass filters
  ok &= write_reg(0x22, 0x0A);
  ok &= write_reg(0x20, 0x0A);
  ok &= write_reg(0x21, 0x2A);
  ok &= update_reg(0x08, 0x01, 0x00);  // Slave
  ok &= write_reg(0x40, 0x43);         // Analog power, VDDA 3.3 V
  ok &= write_reg(0x41, 0x70);         // MIC1/2 bias 2.87 V
  ok &= write_reg(0x42, 0x70);
  ok &= write_reg(0x07, 0x20);         // OSR
  ok &= write_reg(0x02, 0xC1);         // Dividers, DLL; clears the state
  ok &= select_mics(code);
  uint8_t clock_off = 0;
  ok &= read_reg(0x01, &clock_off);
  // 16-bit, I2S.
  ok &= update_reg(0x11, 0xE0, 0x60);
  ok &= update_reg(0x11, 0x03, 0x00);
  // Start.
  ok &= write_reg(0x01, clock_off);
  ok &= write_reg(0x06, 0x00);
  ok &= write_reg(0x40, 0x43);
  for (uint8_t r = 0x47; r <= 0x4A; r++) ok &= write_reg(r, kMicPower);
  ok &= select_mics(code);
  ok &= write_reg(0x40, 0x43);
  ok &= write_reg(0x00, 0x71);
  ok &= write_reg(0x00, 0x41);
  if (ok) {
    ESP_LOGI(kTag, "ES7210 at 0x%02x, MIC1-4 in TDM, gain code %u", g_address,
             code);
  } else {
    ESP_LOGE(kTag, "ES7210 did not take its settings");
  }
  es7210_log_registers();
  return ok;
}

bool es7210_set_gain(float gain_db) {
  const uint8_t code = mic::es7210_gain_code(gain_db);
  const bool ok =
      update_reg(0x43, 0x0F, code) && update_reg(0x44, 0x0F, code);
  uint8_t r43 = 0, r44 = 0;
  read_reg(0x43, &r43);
  read_reg(0x44, &r44);
  ESP_LOGI(kTag, "Gain code %u: reg43 %02x reg44 %02x%s", code, r43, r44,
           ok ? "" : " (a write failed)");
  return ok;
}

void es7210_log_registers() {
  // What the chip holds, for the boot log: clocks, mode, format, power,
  // bias and the two PGAs. 0xEE: the read failed.
  static const uint8_t kRegs[] = {0x00, 0x01, 0x02, 0x06, 0x07, 0x08,
                                  0x11, 0x12, 0x40, 0x41, 0x42, 0x43,
                                  0x44, 0x47, 0x48, 0x4B, 0x4C};
  char buf[200];
  int n = 0;
  for (uint8_t r : kRegs) {
    uint8_t v = 0xEE;
    if (!read_reg(r, &v)) v = 0xEE;
    n += snprintf(buf + n, sizeof(buf) - n, " %02x=%02x", r, v);
    if (n >= (int)sizeof(buf)) break;
  }
  ESP_LOGI(kTag, "ES7210 registers:%s", buf);
}

void es7210_stop() {
  for (uint8_t r = 0x47; r <= 0x4C; r++) write_reg(r, 0xFF);
  write_reg(0x40, 0xC0);
  write_reg(0x01, 0x7F);
  write_reg(0x06, 0x07);
}

}  // namespace esp
}  // namespace crewradio
#endif
