#include "lcd_readout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace skgauges {
namespace ui {

namespace {

// Digit height (the font size) as a fraction of the panel height.
constexpr float kDigitFont = 0.70f;

}  // namespace

LcdReadout::LcdReadout(lv_obj_t* parent, int32_t cx, int32_t cy, int32_t w,
                       int32_t h, uint8_t slots)
    : slots_(slots), small_font_(font_for_px(h * 30 / 100)) {
  const int32_t want = (int32_t)lroundf(h * kDigitFont);
  digit_font_ = font_for_px(want < kMaxFontPx ? want : kMaxFontPx);
  digit_scale_ = want > kMaxFontPx ? (float)want / kMaxFontPx : 1.0f;

  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_, w, h);
  lv_obj_set_pos(obj_, cx - w / 2, cy - h / 2);
  lv_obj_set_style_radius(obj_, h / 8, 0);
  lv_obj_set_style_bg_opa(obj_, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(obj_, lv_color_hex(color::kLcdTop), 0);
  lv_obj_set_style_bg_grad_color(obj_, lv_color_hex(color::kLcdBottom), 0);
  lv_obj_set_style_bg_grad_dir(obj_, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(obj_, h / 28 > 1 ? h / 28 : 1, 0);
  lv_obj_set_style_border_color(obj_, lv_color_hex(0x2C3028), 0);
  lv_obj_set_style_border_opa(obj_, LV_OPA_80, 0);
  lv_obj_add_event_cb(obj_, draw_cb, LV_EVENT_DRAW_MAIN_END, this);
  set_dashes();
}

void LcdReadout::set_text(const char* text, bool with_unit) {
  // More characters than the readout has room for: "EEE", distinct from the
  // no-data dashes (a '.' takes no slot).
  char shown[sizeof(text_)];
  size_t chars = 0;
  for (const char* p = text; *p != '\0'; p++) {
    if (*p != '.') chars++;
  }
  if (chars > slots_) {
    const size_t n = slots_ < 15 ? slots_ : 15;
    memset(shown, 'E', n);
    shown[n] = '\0';
  } else {
    snprintf(shown, sizeof(shown), "%.15s", text);
  }
  if (with_unit) {
    strncat(shown, unit_, sizeof(shown) - strlen(shown) - 1);
  }
  if (digits_ && strcmp(shown, text_) == 0) {
    return;
  }
  strcpy(text_, shown);
  digits_.reset(new TextImage(text_, digit_font_, color::kLcdInk, true));
  lv_obj_invalidate(obj_);
}

void LcdReadout::set_value(float value, uint8_t decimals, bool zero_pad) {
  if (!isfinite(value)) {
    set_dashes();
    return;
  }
  char buf[24];
  if (zero_pad) {
    snprintf(buf, sizeof(buf), "%0*.*f", slots_ + (decimals ? 1 : 0),
             decimals, value);
  } else {
    snprintf(buf, sizeof(buf), "%.*f", decimals, value);
  }
  // "-0" / "-0.0" reads oddly on an instrument.
  if (buf[0] == '-' && strspn(buf + 1, "0.") == strlen(buf + 1)) {
    memmove(buf, buf + 1, strlen(buf));
  }
  set_text(buf, true);
}

void LcdReadout::set_dashes() {
  char buf[16];
  uint8_t n = slots_ < sizeof(buf) - 1 ? slots_ : sizeof(buf) - 1;
  memset(buf, '-', n);
  buf[n] = '\0';
  set_text(buf, false);
}

void LcdReadout::set_unit(const char* text) {
  snprintf(unit_, sizeof(unit_), "%s", text);  // Shown from the next value
}

void LcdReadout::set_prefix(const char* text) {
  if (strncmp(text, prefix_, sizeof(prefix_)) == 0) return;
  strncpy(prefix_, text, sizeof(prefix_) - 1);
  prefix_[sizeof(prefix_) - 1] = '\0';
  lv_obj_invalidate(obj_);
}

void LcdReadout::set_suffix(const char* text) {
  if (strncmp(text, suffix_, sizeof(suffix_)) == 0) return;
  strncpy(suffix_, text, sizeof(suffix_) - 1);
  suffix_[sizeof(suffix_) - 1] = '\0';
  lv_obj_invalidate(obj_);
}

void LcdReadout::draw_cb(lv_event_t* e) {
  auto* self = static_cast<LcdReadout*>(lv_event_get_user_data(e));
  lv_area_t coords;
  lv_obj_get_coords(self->obj_, &coords);
  self->draw(lv_event_get_layer(e), coords);
}

void LcdReadout::draw(lv_layer_t* layer, const lv_area_t& coords) const {
  const float h = lv_area_get_height(&coords);
  const float pad = h * 0.18f;

  // Prefix (left) and suffix (right) texts.
  float right = coords.x2 - pad;
  if (suffix_[0] != '\0') {
    lv_point_t size;
    lv_text_get_size(&size, suffix_, small_font_, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    draw_text(layer, suffix_, small_font_, color::kLcdInk,
              (int32_t)(right - size.x / 2.0f),
              (int32_t)(coords.y1 + h * 0.62f));
    right -= size.x + pad * 0.5f;
  }
  float left = coords.x1 + pad;
  if (prefix_[0] != '\0') {
    lv_point_t size;
    lv_text_get_size(&size, prefix_, small_font_, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    draw_text(layer, prefix_, small_font_, color::kLcdInk,
              (int32_t)(coords.x1 + pad + size.x / 2.0f),
              (int32_t)(coords.y1 + h * 0.36f));
    left += size.x + pad * 0.5f;
  }

  // The value, right-aligned between them; shrunk if it does not fit.
  if (!digits_ || !digits_->valid() || right <= left) {
    return;
  }
  float scale = digit_scale_;
  const float avail = right - left;
  if (digits_->width() * scale > avail) {
    scale = avail / digits_->width();
  }
  const float w = digits_->width() * scale;
  digits_->draw(layer, right - w / 2.0f, coords.y1 + h / 2.0f, 0.0f,
                LV_OPA_COVER, scale);
}

}  // namespace ui
}  // namespace skgauges
