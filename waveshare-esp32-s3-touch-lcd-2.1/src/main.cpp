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

#include "sensesp_base_app.h"
#include "signalk_gauges.h"

using esp_panel::board::Board;

namespace {
Board* board = nullptr;
}  // namespace

void setup() {
  Serial.begin(115200);
  sensesp::SetupLogging(ESP_LOG_INFO);

  board = new Board();
  if (!board->init() || !board->begin()) {
    // Nothing useful can run without the display; try again from scratch.
    ESP_LOGE("main", "Display board init failed, restarting");
    delay(1000);
    esp_restart();
  }
  skgauges::lvgl_port_init(board, LVGL_BUFFER_LINES);

  skgauges::gauge_app_begin({
      GAUGE_HOSTNAME,
      GAUGE_UNIT_NAME,
      GAUGE_SIDE,
      GAUGE_ADDRESS,
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
