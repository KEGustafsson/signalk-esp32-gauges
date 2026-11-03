// Signal K steel gauges -- Waveshare ESP32-S3-Touch-LCD-2.1.
//
// The display, touch, backlight and IO expander are brought up by
// ESP32_Display_Panel from its built-in board file for this board
// (BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_2_1, selected in platformio.ini):
//   ST7701 480x480 RGB panel, 3-wire SPI init with CS on TCA9554 EXIO3,
//   LCD reset EXIO1, CST820 touch on I2C (SDA 15, SCL 7) with reset EXIO2,
//   backlight PWM on GPIO6.
// Everything else (SensESP, Signal K, the gauge pages) is in ../common.

#include <Arduino.h>

#include <esp_display_panel.hpp>

#include "board_start.h"
#include "sensesp_base_app.h"
#include "signalk_gauges.h"

using esp_panel::board::Board;

namespace {
Board* board = nullptr;
// LVGL draw buffer: 480 x 20 px (RGB565, 19 KB of internal RAM). Kept small:
// the RGB panel's 20-line bounce buffer and SensESP's tasks need the
// internal RAM (common/src/board_start.cpp).
constexpr int kBufferLines = 20;
}  // namespace

void setup() {
  Serial.begin(115200);
  sensesp::SetupLogging(ESP_LOG_INFO);

  board = skgauges::start_board(0);  // CST820: one address
  skgauges::lvgl_port_init(board, kBufferLines);

  // One firmware for both sides: the unit's saved hostname says which it is
  // (common/src/unit_identity.h).
  skgauges::gauge_app_begin({
      "21",
      "2.1in",
      SECRET_OTA_HOST_S3_STBD,
      SECRET_OTA_HOST_S3_PORT,
      0,  // SensESP button: the BOOT key (GPIO0)
      [](int percent) {
        auto* backlight = board->getBacklight();
        if (backlight != nullptr) {
          backlight->setBrightness(percent);
        }
      },
  });
}

void loop() { skgauges::gauge_app_loop(); }
