#pragma once

// Brings up an ESP32_Display_Panel board (display, touch, backlight) and
// never leaves a unit dark because of touch.
//
// Board::begin() fails as a whole when the touch controller does not answer.
// A gauge is still useful without touch, so start_board() tries, one boot
// each (the attempt survives esp_restart() in RTC memory):
//   1. the touch address that worked last time (remembered in NVS),
//   2. the other address, if the board has one (GT911: 0x5D or 0x14),
//   3. the board without touch.
// A display that fails on every attempt keeps restarting.

#include <stdint.h>

namespace esp_panel {
namespace board {
class Board;
}
}  // namespace esp_panel

namespace skgauges {

// alternate_touch_address: the touch controller's other I2C address, or 0 if
// it has only the one in the board file.
esp_panel::board::Board* start_board(uint8_t alternate_touch_address);

}  // namespace skgauges
