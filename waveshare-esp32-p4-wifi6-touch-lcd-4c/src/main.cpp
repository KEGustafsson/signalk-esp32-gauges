// Signal K steel gauges -- Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.
//
// The display, touch and backlight are brought up by ESP32_Display_Panel from
// include/esp_panel_board_custom_conf.h (values from Waveshare's BSP):
//   JD9365 720x720 panel on 2-lane MIPI-DSI, LCD reset GPIO27,
//   GT911-compatible touch on I2C (SDA 7, SCL 8), backlight PWM on GPIO26.
// WiFi goes through the on-board ESP32-C6 (esp-hosted over SDIO), which
// Arduino's WiFi.h -- and so SensESP -- uses transparently.
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
  board->init();
  if (!board->begin()) {
    ESP_LOGE("main", "Display board init failed");
  }
  skgauges::lvgl_port_init(board, LVGL_BUFFER_LINES);

  skgauges::gauge_app_begin({
      GAUGE_HOSTNAME,
      GAUGE_UNIT_NAME,
      GAUGE_SIDE,
      GAUGE_ADDRESS,
      -1,  // No SensESP button
      [](int percent) {
        auto* backlight = board->getBacklight();
        if (backlight != nullptr) {
          backlight->setBrightness(percent);
        }
      },
  });
}

void loop() { skgauges::gauge_app_loop(); }
