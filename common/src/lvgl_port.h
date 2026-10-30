#pragma once

// LVGL 9 display + touch driver on top of an ESP32_Display_Panel board.
//
// Both Waveshare boards are driven through ESP32_Display_Panel (the 2.1" from
// its built-in board table, the 4" from a custom board file), so one port
// serves both: LVGL renders into partial buffers and the flush callback hands
// each area to LCD::drawBitmap(), which copies it into the RGB / MIPI-DSI
// panel's frame buffer.
//
// LVGL runs on the Arduino loop task together with SensESP's event loop, so
// no locking is needed: call lv_timer_handler() from loop().

#include <lvgl.h>

namespace esp_panel {
namespace board {
class Board;
}
}  // namespace esp_panel

namespace skgauges {

// buffer_lines: height of the LVGL draw buffer in pixel rows.
lv_display_t* lvgl_port_init(esp_panel::board::Board* board,
                             int buffer_lines);

}  // namespace skgauges
