#pragma once

// Backlit-LCD style numeric readout set into the dial face: a grey-green
// panel with the value in plain dark digits, right-aligned, as on the
// reference gauges. Optional small prefix/suffix texts (e.g. "°T", "PORT").
//
// The digits come from a Montserrat font; a panel taller than the largest
// font allows (the 4") draws them from a scaled text image. The image is
// rebuilt only when the text changes: the needle sweeps over the readout
// and redraws it far more often than the value changes.

#include <lvgl.h>
#include <stdint.h>

#include <memory>

#include "draw_utils.h"

namespace skgauges {
namespace ui {

class LcdReadout {
 public:
  // (cx, cy) is the centre of the LCD; slots is how many characters the
  // value may take (more show as "EEE", distinct from the no-data dashes).
  LcdReadout(lv_obj_t* parent, int32_t cx, int32_t cy, int32_t w, int32_t h,
             uint8_t slots);

  void set_value(float value, uint8_t decimals, bool zero_pad = false);
  void set_dashes();
  void set_prefix(const char* text);
  void set_suffix(const char* text);
  // Drawn right after the digits in their font (e.g. "°", which then sits at
  // the top of the digits); not with the dashes, and takes no slot.
  void set_unit(const char* text);

 private:
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer, const lv_area_t& coords) const;
  void set_text(const char* text, bool with_unit);

  lv_obj_t* obj_;
  uint8_t slots_;
  const lv_font_t* digit_font_;
  float digit_scale_;  // > 1 when the digits are larger than the font
  const lv_font_t* small_font_;
  std::unique_ptr<TextImage> digits_;
  char text_[24] = "";
  char unit_[8] = "";
  char prefix_[12] = "";
  char suffix_[12] = "";
};

}  // namespace ui
}  // namespace skgauges
