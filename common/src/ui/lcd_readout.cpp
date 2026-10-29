#include "lcd_readout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "draw_utils.h"

namespace skgauges {
namespace ui {

namespace {

// Segment bits: a=0 (top), b=1, c=2, d=3 (bottom), e=4, f=5, g=6 (middle).
uint8_t segments_for(char c) {
  switch (c) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    case '-': return 0x40;
    case 'E': return 0x79;
    default: return 0x00;
  }
}

struct Cell {
  uint8_t segments;
  bool dot;
};

void fill_pill(lv_layer_t* layer, float x1, float y1, float x2, float y2,
               lv_color_t color, lv_opa_t opa) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.bg_color = color;
  dsc.bg_opa = opa;
  const float thickness = fminf(x2 - x1, y2 - y1);
  dsc.radius = (int32_t)(thickness / 2.0f);
  lv_area_t a = {(int32_t)lroundf(x1), (int32_t)lroundf(y1),
                 (int32_t)lroundf(x2) - 1, (int32_t)lroundf(y2) - 1};
  lv_draw_rect(layer, &dsc, &a);
}

void draw_digit(lv_layer_t* layer, float x, float y, float w, float h,
                uint8_t segments, lv_color_t ink, lv_opa_t opa) {
  const float t = w * 0.21f;     // Segment thickness
  const float s = t * 0.18f;     // Gap between segments
  const float mid = y + h / 2.0f;
  const float hx1 = x + t * 0.5f + s;
  const float hx2 = x + w - t * 0.5f - s;
  struct Rect {
    float x1, y1, x2, y2;
  };
  const Rect r[7] = {
      {hx1, y, hx2, y + t},                                  // a
      {x + w - t, y + t * 0.5f + s, x + w, mid - s},         // b
      {x + w - t, mid + s, x + w, y + h - t * 0.5f - s},     // c
      {hx1, y + h - t, hx2, y + h},                          // d
      {x, mid + s, x + t, y + h - t * 0.5f - s},             // e
      {x, y + t * 0.5f + s, x + t, mid - s},                 // f
      {hx1, mid - t * 0.5f, hx2, mid + t * 0.5f},            // g
  };
  for (int i = 0; i < 7; i++) {
    if (segments & (1u << i)) {
      fill_pill(layer, r[i].x1, r[i].y1, r[i].x2, r[i].y2, ink, opa);
    }
  }
}

}  // namespace

LcdReadout::LcdReadout(lv_obj_t* parent, int32_t cx, int32_t cy, int32_t w,
                       int32_t h, uint8_t slots)
    : slots_(slots), small_font_(font_for_px(h * 30 / 100)) {
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

void LcdReadout::set_text(const char* text) {
  if (strncmp(text, text_, sizeof(text_)) == 0) {
    return;
  }
  strncpy(text_, text, sizeof(text_) - 1);
  text_[sizeof(text_) - 1] = '\0';
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
  set_text(buf);
}

void LcdReadout::set_dashes() {
  char buf[16];
  uint8_t n = slots_ < sizeof(buf) - 1 ? slots_ : sizeof(buf) - 1;
  memset(buf, '-', n);
  buf[n] = '\0';
  set_text(buf);
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
  const float w = lv_area_get_width(&coords);
  const float h = lv_area_get_height(&coords);
  const float pad = h * 0.14f;
  const lv_color_t ink = lv_color_hex(color::kLcdInk);

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

  // Split the text into digit cells; a '.' lights the previous cell's dot.
  Cell cells[16];
  int count = 0;
  for (const char* p = text_; *p != '\0' && count < 16; p++) {
    if (*p == '.') {
      if (count > 0) cells[count - 1].dot = true;
      continue;
    }
    cells[count].segments = segments_for(*p);
    cells[count].dot = false;
    count++;
  }
  if (count > slots_) {  // Does not fit: "EEEE", distinct from no-data dashes
    count = slots_;
    for (int i = 0; i < count; i++) cells[i] = {segments_for('E'), false};
  }

  const float digit_h = h * 0.66f;
  float digit_w = digit_h * 0.52f;
  float gap = digit_w * 0.34f;
  // Shrink if the slots do not fit between the prefix and the suffix.
  const float avail = right - left;
  if (avail <= 0.0f) {  // Prefix and suffix fill the box: nothing fits
    return;
  }
  const float needed = slots_ * digit_w + (slots_ - 1) * gap;
  if (needed > avail && needed > 0) {
    digit_w *= avail / needed;
    gap *= avail / needed;
  }
  const float y = coords.y1 + (h - digit_h) / 2.0f;
  const float dot = digit_w * 0.2f;

  for (int slot = 0; slot < slots_; slot++) {
    // Right-align: the last cell goes into the last slot.
    const int cell_index = count - (slots_ - slot);
    const float x = right - (slots_ - slot) * digit_w -
                    (slots_ - slot - 1) * gap;
    draw_digit(layer, x, y, digit_w, digit_h, 0x7F, ink, LV_OPA_10);
    if (cell_index >= 0) {
      draw_digit(layer, x, y, digit_w, digit_h, cells[cell_index].segments,
                 ink, LV_OPA_90);
      if (cells[cell_index].dot) {
        const float dx = x + digit_w + (gap - dot) / 2.0f;
        fill_pill(layer, dx, y + digit_h - dot, dx + dot, y + digit_h, ink,
                  LV_OPA_90);
      }
    }
  }
}

}  // namespace ui
}  // namespace skgauges
