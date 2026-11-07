#pragma once

// Backlit-LCD style numeric readout set into the dial face: a grey-green
// panel with the value in plain dark digits, right-aligned, as on the
// reference gauges. Optional small prefix/suffix texts (e.g. "°T", "PORT").
//
// The digits come from a Montserrat font, as a text image rebuilt when the
// text changes (the needle sweeps over the readout and redraws it far more
// often than the value changes). A panel taller than the largest font
// allows (the 4") draws them character by character instead, each one
// scaled once, the first time it is needed: no transform at a redraw, and
// none at a value change either (an rpm readout changes every step).

#include <lvgl.h>
#include <stdint.h>

#include <memory>
#include <vector>

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

  // Draws the panel (gradient, rounded corners, border) on the page's face
  // canvas, once, and from then on draws only the texts: a value change or
  // the needle passing redraws the digits, not the panel under them. Call
  // while the face is drawn (before finish_canvas()).
  void move_background_into(lv_layer_t* face);

 private:
  static void draw_cb(lv_event_t* e);
  void draw(lv_layer_t* layer, const lv_area_t& coords) const;
  void set_text(const char* text, bool with_unit);
  // The scaled characters (digit_scale_ > 1): text_ in tabular cells,
  // right-aligned at `right`, if it fits in `avail`; false if not.
  bool draw_glyphs(lv_layer_t* layer, float right, float avail,
                   float cy) const;
  struct Glyph {
    uint32_t cp;
    int32_t w;  // The font's width for it, unscaled
    std::unique_ptr<TextImage> image;  // Scaled
  };
  const Glyph* glyph(uint32_t cp) const;

  lv_obj_t* obj_;
  lv_area_t area_;  // The panel, screen px
  uint8_t slots_;
  const lv_font_t* digit_font_;
  float digit_scale_;  // > 1 when the digits are larger than the font
  const lv_font_t* small_font_;
  // The text as one image: always where the digits are not scaled; where
  // they are, made only if the characters do not fit (then scaled down).
  mutable std::unique_ptr<TextImage> digits_;
  mutable std::vector<Glyph> glyphs_;
  int32_t cell_ = 0;  // A digit's cell, unscaled (the widest digit)
  char text_[24] = "";
  char unit_[8] = "";
  char prefix_[12] = "";
  char suffix_[12] = "";
};

}  // namespace ui
}  // namespace skgauges
