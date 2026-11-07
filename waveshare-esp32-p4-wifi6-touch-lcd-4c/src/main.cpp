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

#include "board_start.h"
#include "crew_view_source.h"
#include "lvgl_port.h"
#include "glue/crew_radio_card.h"
#include "sensesp_base_app.h"
#include "signalk_gauges.h"

using esp_panel::board::Board;

namespace {
Board* board = nullptr;
// The GT911-compatible touch chip answers at 0x5D on most boards and at 0x14
// on some (Waveshare's BSP probes both); start_board() tries both.
constexpr uint8_t kGt911BackupAddress = 0x14;
// LVGL draw buffer: 720 x 72 px (RGB565, 101 KB), in PSRAM: internal DMA RAM
// is for the WiFi link to the C6 (common/src/lvgl_port.cpp).
constexpr int kBufferLines = 72;
}  // namespace

void setup() {
  Serial.begin(115200);
  // USB Serial/JTAG: never block the loop waiting for a host that is not
  // reading (the default 100 ms per write would stall the gauge).
  Serial.setTxTimeoutMs(0);
  sensesp::SetupLogging(ESP_LOG_INFO);

  board = skgauges::start_board(kGt911BackupAddress);
  // The Crew Radio's view for the screen (halo, Crew page), set before the
  // UI is built so the halo is made; off until the radio is on in its card.
  skgauges::ui::set_crew_view_source(skgauges::crew_view_from_radio);
  skgauges::lvgl_port_init(board, kBufferLines, true);

  // One firmware for both sides: the unit's saved hostname says which it is
  // (common/src/unit_identity.h).
  skgauges::gauge_app_begin({
      "4c",
      "4in",
      SECRET_OTA_HOST_P4_STBD,
      SECRET_OTA_HOST_P4_PORT,
      -1,  // No SensESP button
      [](int percent) {
        auto* backlight = board->getBacklight();
        if (backlight != nullptr) {
          backlight->setBrightness(percent);
        }
      },
  });

  // The Crew Radio on this board's speaker and mic (p4-gauge-crew-radio):
  // off until switched on in its card. After the board, whose touch setup
  // installs the I2C driver the codecs are set up through; a two-finger
  // hold on the screen is one of its talk keys.
  crewradio::glue::begin(skgauges::touch_fingers);
}

void loop() { skgauges::gauge_app_loop(); }
