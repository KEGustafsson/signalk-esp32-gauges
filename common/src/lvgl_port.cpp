#include "lvgl_port.h"

#include <Arduino.h>
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>

namespace skgauges {

namespace {

using esp_panel::board::Board;
using esp_panel::drivers::LCD;
using esp_panel::drivers::Touch;
using esp_panel::drivers::TouchPoint;

uint32_t tick_cb() { return millis(); }

void log_cb(lv_log_level_t level, const char* buf) {
  (void)level;
  Serial.print(buf);
}

void flush_cb(lv_display_t* display, const lv_area_t* area,
              uint8_t* px_map) {
  auto* lcd = static_cast<LCD*>(lv_display_get_user_data(display));
  // Blocking (-1): the buffer is handed back to LVGL only once the panel
  // driver is done with it. For RGB panels this is a memcpy anyway.
  lcd->drawBitmap(area->x1, area->y1, lv_area_get_width(area),
                  lv_area_get_height(area), px_map, -1);
  lv_display_flush_ready(display);
}

void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* touch = static_cast<Touch*>(lv_indev_get_user_data(indev));
  TouchPoint point;
  if (touch != nullptr && touch->readPoints(&point, 1, 0) > 0) {
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void* alloc_draw_buffer(size_t bytes) {
  // Internal RAM renders fastest; fall back to PSRAM if WiFi and friends
  // have left too little.
  void* buf = heap_caps_aligned_alloc(64, bytes,
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (buf == nullptr) {
    buf = heap_caps_aligned_alloc(64, bytes,
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  }
  return buf;
}

}  // namespace

lv_display_t* lvgl_port_init(Board* board, int buffer_lines) {
  LCD* lcd = board->getLCD();
  const int32_t width = lcd->getFrameWidth();
  const int32_t height = lcd->getFrameHeight();

  lv_init();
  lv_tick_set_cb(tick_cb);
  lv_log_register_print_cb(log_cb);

  lv_display_t* display = lv_display_create(width, height);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  const size_t bytes = (size_t)width * buffer_lines * 2;
  void* buf1 = alloc_draw_buffer(bytes);
  void* buf2 = alloc_draw_buffer(bytes);
  LV_ASSERT_MALLOC(buf1);
  LV_ASSERT_MALLOC(buf2);
  lv_display_set_buffers(display, buf1, buf2, bytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_user_data(display, lcd);
  lv_display_set_flush_cb(display, flush_cb);

  Touch* touch = board->getTouch();
  if (touch != nullptr) {
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(indev, touch);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, display);
  } else {
    Serial.println("lvgl_port: no touch controller, swiping disabled");
  }
  return display;
}

}  // namespace skgauges
