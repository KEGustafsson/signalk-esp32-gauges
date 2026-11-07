#include "lcd_readout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace skgauges {
namespace ui {

namespace {

// Digit height (the font size) as a fraction of the panel height.
constexpr float kDigitFont = 0.70f;

// The code point at *p (UTF-8), advancing *p past it.
uint32_t next_code_point(const char** p) {
  const uint8_t c = (uint8_t)**p;
  const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = extra == 0 ? c : (c & (0x3F >> extra));
  (*p)++;
  for (int k = 0; k < extra && **p != '\0'; k++, (*p)++) {
    cp = (cp << 6) | ((uint8_t)**p & 0x3F);
  }
  return cp;
}

}  // namespace

LcdReadout::LcdReadout(lv_obj_t* parent, int32_t cx, int32_t cy, int32_t w,
                       int32_t h, uint8_t slots)
    : slots_(slots), small_font_(font_for_px(h * 30 / 100)) {
  const int32_t want = (int32_t)lroundf(h * kDigitFont);
  digit_font_ = font_for_px(want < kMaxFontPx ? want : kMaxFontPx);
  digit_scale_ = want > kMaxFontPx ? (float)want / kMaxFontPx : 1.0f;
  for (uint32_t d = '0'; d <= '9'; d++) {
    const int32_t w = lv_font_get_glyph_width(digit_font_, d, 0);
    if (w > cell_) cell_ = w;
  }

  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_, w, h);
  lv_obj_set_pos(obj_, cx - w / 2, cy - h / 2);
  area_ = {cx - w / 2, cy - h / 2, cx - w / 2 + w - 1, cy - h / 2 + h - 1};
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
  if (text_[0] != '\0' && strcmp(shown, text_) == 0) {
    return;
  }
  strcpy(text_, shown);
  if (digit_scale_ > 1.0f) {
    digits_.reset();  // Drawn from the characters (made if they do not fit)
  } else {
    digits_.reset(new TextImage(text_, digit_font_, color::kLcdInk, true));
  }
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

const LcdReadout::Glyph* LcdReadout::glyph(uint32_t cp) const {
  for (const Glyph& g : glyphs_) {
    if (g.cp == cp) return &g;
  }
  // UTF-8 for the one code point (the digits, '.', '-', 'E', the unit).
  char one[5] = {};
  if (cp < 0x80) {
    one[0] = (char)cp;
  } else if (cp < 0x800) {
    one[0] = (char)(0xC0 | (cp >> 6));
    one[1] = (char)(0x80 | (cp & 0x3F));
  } else {
    one[0] = (char)(0xE0 | (cp >> 12));
    one[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    one[2] = (char)(0x80 | (cp & 0x3F));
  }
  Glyph g;
  g.cp = cp;
  g.w = lv_font_get_glyph_width(digit_font_, cp, 0);
  g.image.reset(new TextImage(one, digit_font_, color::kLcdInk, false,
                              glyph_handlers()));
  if (g.image->valid() && !g.image->bake_scale(digit_scale_)) {
    g.image.reset();  // No memory: the whole text is drawn instead
  }
  glyphs_.push_back(std::move(g));
  return &glyphs_.back();
}

bool LcdReadout::draw_glyphs(lv_layer_t* layer, float right, float avail,
                             float cy) const {
  // As TextImage lays out tabular digits: each digit centred in a cell as
  // wide as the widest, the rest at their own width; scaled.
  const float s = digit_scale_;
  float width = 4.0f * s;  // TextImage's padding, both sides
  for (const char* p = text_; *p != '\0';) {
    const uint32_t cp = next_code_point(&p);
    const bool digit = cp >= '0' && cp <= '9';
    width += (digit ? cell_ : lv_font_get_glyph_width(digit_font_, cp, 0)) * s;
  }
  if (width > avail) return false;
  float x = right - width + 2.0f * s;
  for (const char* p = text_; *p != '\0';) {
    const uint32_t cp = next_code_point(&p);
    const Glyph* g = glyph(cp);
    if (g == nullptr || !g->image || !g->image->valid()) {
      if (g != nullptr && g->w == 0) continue;  // Nothing to draw
      return false;
    }
    const bool digit = cp >= '0' && cp <= '9';
    const float advance = (digit ? cell_ : g->w) * s;
    g->image->draw(layer, x + advance / 2.0f, cy, 0.0f, LV_OPA_COVER, 1.0f);
    x += advance;
  }
  return true;
}

void LcdReadout::move_background_into(lv_layer_t* face) {
  const int32_t h = lv_area_get_height(&area_);
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  // As the object's own style draws it (the constructor).
  dsc.radius = h / 8;
  dsc.bg_opa = LV_OPA_COVER;
  dsc.bg_color = lv_color_hex(color::kLcdTop);
  dsc.bg_grad.dir = LV_GRAD_DIR_VER;
  dsc.bg_grad.stops_count = 2;
  dsc.bg_grad.stops[0].color = lv_color_hex(color::kLcdTop);
  dsc.bg_grad.stops[0].opa = LV_OPA_COVER;
  dsc.bg_grad.stops[0].frac = 0;
  dsc.bg_grad.stops[1].color = lv_color_hex(color::kLcdBottom);
  dsc.bg_grad.stops[1].opa = LV_OPA_COVER;
  dsc.bg_grad.stops[1].frac = 255;
  dsc.border_width = h / 28 > 1 ? h / 28 : 1;
  dsc.border_color = lv_color_hex(0x2C3028);
  dsc.border_opa = LV_OPA_80;
  lv_draw_rect(face, &dsc, &area_);
  lv_obj_set_style_bg_opa(obj_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(obj_, 0, 0);
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
  if (right <= left) {
    return;
  }
  const float avail = right - left;
  if (digit_scale_ > 1.0f &&
      draw_glyphs(layer, right, avail, coords.y1 + h / 2.0f)) {
    return;
  }
  if (!digits_) {
    digits_.reset(new TextImage(text_, digit_font_, color::kLcdInk, true));
  }
  if (!digits_->valid()) {
    return;
  }
  float scale = digit_scale_;
  if (digits_->width() * scale > avail) {
    scale = avail / digits_->width();
  }
  const float w = digits_->width() * scale;
  digits_->draw(layer, right - w / 2.0f, coords.y1 + h / 2.0f, 0.0f,
                LV_OPA_COVER, scale);
}

}  // namespace ui
}  // namespace skgauges
