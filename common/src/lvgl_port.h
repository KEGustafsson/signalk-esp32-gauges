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
// buffer_in_psram: never put the draw buffer in internal RAM (the 4": its
// WiFi link needs the internal DMA RAM, lvgl_port.cpp).
lv_display_t* lvgl_port_init(esp_panel::board::Board* board,
                             int buffer_lines, bool buffer_in_psram = false);

// Fingers on the screen at the last touch read: 0, 1 or 2 (a panel that
// reports one point never says 2). While two are down LVGL is told the
// screen is released, so a two-finger hold (the 4"'s Crew Radio talk key)
// never swipes a page or starts the brightness hold. From the loop task.
int touch_fingers();

}  // namespace skgauges
