#pragma once

// Backlit-LCD style numeric readout set into the dial face.
//
// Digits are drawn as seven-segment shapes rather than a font, so they scale
// to any panel size and keep the classic instrument look, including the faint
// "ghost" segments of a real LCD. Optional small prefix/suffix texts (e.g.
// "°T", "PORT") use a Montserrat font.

#include <lvgl.h>
#include <stdint.h>

namespace skgauges {
namespace ui {

class LcdReadout {
 public:
  // (cx, cy) is the centre of the LCD; slots is the number of digit cells.
  LcdReadout(lv_obj_t* parent, int32_t cx, int32_t cy, int32_t w, int32_t h,
             uint8_t slots);

  void set_value(float value, uint8_t decimals, bool zero_pad = false);
  void set_dashes();
  void set_prefix(const char* text);
  void set_suffix(const char* text);

 private:
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer, const lv_area_t& coords) const;
  void set_text(const char* text);

  lv_obj_t* obj_;
  uint8_t slots_;
  const lv_font_t* small_font_;
  char text_[16] = "";
  char prefix_[12] = "";
  char suffix_[12] = "";
};

}  // namespace ui
}  // namespace skgauges
