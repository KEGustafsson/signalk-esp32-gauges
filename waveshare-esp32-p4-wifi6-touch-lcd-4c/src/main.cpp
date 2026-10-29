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

#include <esp_attr.h>
#include <esp_display_panel.hpp>

#include "sensesp_base_app.h"
#include "signalk_gauges.h"

using esp_panel::board::Board;

namespace {
Board* board = nullptr;

// The GT911-compatible touch chip answers at 0x5D on most boards and at 0x14
// on some (Waveshare's BSP probes both). Board::begin() fails if touch does
// not answer, so after a failed start the next boot tries the other address.
// RTC_NOINIT memory survives esp_restart(); the marker value guards against
// power-on garbage.
constexpr uint32_t kUseBackupTouchAddress = 0x14A5;
constexpr uint8_t kGt911BackupAddress = 0x14;
RTC_NOINIT_ATTR uint32_t touch_address_choice;
}  // namespace

void setup() {
  Serial.begin(115200);
  // USB Serial/JTAG: never block the loop waiting for a host that is not
  // reading (the default 100 ms per write would stall the gauge).
  Serial.setTxTimeoutMs(0);
  sensesp::SetupLogging(ESP_LOG_INFO);

  board = new Board();
  bool ok = board->init();
  if (ok && touch_address_choice == kUseBackupTouchAddress) {
    static_cast<esp_panel::drivers::BusI2C*>(board->getTouch()->getBus())
        ->configI2C_Address(kGt911BackupAddress);
  }
  if (!ok || !board->begin()) {
    // Nothing useful can run without the display; try again from scratch,
    // with the other touch address.
    touch_address_choice = touch_address_choice == kUseBackupTouchAddress
                               ? 0
                               : kUseBackupTouchAddress;
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
