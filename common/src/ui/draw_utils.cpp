#include "draw_utils.h"

#include <math.h>

namespace skgauges {
namespace ui {

namespace {

constexpr float kDegToRad = 0.017453292519943295f;

// Renders draw tasks queued on an off-screen layer, like
// lv_canvas_finish_layer() does for a canvas.
void finish_offscreen_layer(lv_layer_t* layer) {
  while (layer->draw_task_head) {
    lv_draw_dispatch_wait_for_request();
    if (!lv_draw_dispatch_layer(lv_display_get_default(), layer)) {
      lv_draw_wait_for_finish();
      lv_draw_dispatch_request();
    }
  }
}

void init_offscreen_layer(lv_layer_t* layer, lv_draw_buf_t* buf) {
  lv_layer_init(layer);
  lv_area_t area = {0, 0, (int32_t)buf->header.w - 1,
                    (int32_t)buf->header.h - 1};
  layer->draw_buf = buf;
  layer->color_format = (lv_color_format_t)buf->header.cf;
  layer->buf_area = area;
  layer->_clip_area = area;
  layer->phy_clip_area = area;
}

void canvas_delete_cb(lv_event_t* e) {
  lv_obj_t* canvas = static_cast<lv_obj_t*>(lv_event_get_target(e));
  lv_draw_buf_t* buf = lv_canvas_get_draw_buf(canvas);
  if (buf != nullptr) {
    lv_image_cache_drop(buf);
    lv_draw_buf_destroy(buf);
  }
}

// 4x4 Bayer matrix, values 0..15.
constexpr uint8_t kBayer[4][4] = {
    {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

}  // namespace

lv_point_precise_t polar(const Geometry& g, float dial_angle_deg,
                         float radius_px) {
  float a = dial_angle_deg * kDegToRad;
  lv_point_precise_t p;
  p.x = g.cx + radius_px * sinf(a);
  p.y = g.cy - radius_px * cosf(a);
  return p;
}

float to_lv_arc_angle(float dial_angle_deg) {
  float a = dial_angle_deg - 90.0f;
  while (a < 0.0f) a += 360.0f;
  while (a >= 360.0f) a -= 360.0f;
  return a;
}

const lv_font_t* font_for_px(int32_t px) {
  // Must match the LV_FONT_MONTSERRAT_* sizes enabled in lv_conf.h.
  static const lv_font_t* const kFonts[] = {
      &lv_font_montserrat_12, &lv_font_montserrat_14, &lv_font_montserrat_16,
      &lv_font_montserrat_18, &lv_font_montserrat_20, &lv_font_montserrat_22,
      &lv_font_montserrat_24, &lv_font_montserrat_26, &lv_font_montserrat_28,
      &lv_font_montserrat_30, &lv_font_montserrat_32, &lv_font_montserrat_34,
      &lv_font_montserrat_36, &lv_font_montserrat_38, &lv_font_montserrat_40,
      &lv_font_montserrat_42, &lv_font_montserrat_44, &lv_font_montserrat_46,
      &lv_font_montserrat_48,
  };
  int index = (int)((px - 12) / 2);
  if (index < 0) index = 0;
  const int last = (int)(sizeof(kFonts) / sizeof(kFonts[0])) - 1;
  if (index > last) index = last;
  return kFonts[index];
}

lv_obj_t* create_canvas(lv_obj_t* parent, int32_t w, int32_t h) {
  lv_draw_buf_t* buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_XRGB8888, 0);
  LV_ASSERT_MALLOC(buf);
  lv_obj_t* canvas = lv_canvas_create(parent);
  lv_canvas_set_draw_buf(canvas, buf);
  lv_canvas_fill_bg(canvas, lv_color_hex(color::kBackground), LV_OPA_COVER);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(canvas, canvas_delete_cb, LV_EVENT_DELETE, nullptr);
  return canvas;
}

void finish_canvas(lv_obj_t* canvas) {
  lv_draw_buf_t* src = lv_canvas_get_draw_buf(canvas);
  if (src == nullptr || src->header.cf != LV_COLOR_FORMAT_XRGB8888) {
    return;
  }
  const uint32_t w = src->header.w;
  const uint32_t h = src->header.h;
  lv_draw_buf_t* dst = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
  if (dst == nullptr) {
    return;  // Keep drawing from the 32-bit buffer
  }
  for (uint32_t y = 0; y < h; y++) {
    const uint8_t* in = src->data + y * src->header.stride;
    uint16_t* out = reinterpret_cast<uint16_t*>(dst->data +
                                                y * dst->header.stride);
    for (uint32_t x = 0; x < w; x++) {
      // XRGB8888 in memory: B, G, R, X. Add a threshold below one output
      // step before truncating: 5-bit red/blue step 8, 6-bit green step 4.
      const uint8_t t = kBayer[y & 3][x & 3];
      const uint32_t b = LV_MIN(255u, (uint32_t)in[0] + (t >> 1));
      const uint32_t g = LV_MIN(255u, (uint32_t)in[1] + (t >> 2));
      const uint32_t r = LV_MIN(255u, (uint32_t)in[2] + (t >> 1));
      out[x] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      in += 4;
    }
  }
  lv_canvas_set_draw_buf(canvas, dst);
  lv_image_cache_drop(src);
  lv_draw_buf_destroy(src);
}

void draw_circle_grad(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r,
                      const uint32_t* colors, const uint8_t* fracs,
                      int stops) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_opa = LV_OPA_COVER;
  if (stops > LV_GRADIENT_MAX_STOPS) stops = LV_GRADIENT_MAX_STOPS;
  dsc.bg_grad.dir = LV_GRAD_DIR_VER;
  dsc.bg_grad.stops_count = (uint8_t)stops;
  for (int i = 0; i < stops; i++) {
    dsc.bg_grad.stops[i].color = lv_color_hex(colors[i]);
    dsc.bg_grad.stops[i].opa = LV_OPA_COVER;
    dsc.bg_grad.stops[i].frac = fracs[i];
  }
  lv_area_t area = {cx - r, cy - r, cx + r - 1, cy + r - 1};
  lv_draw_rect(layer, &dsc, &area);
}

void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r,
                 uint32_t color, lv_opa_t opa) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = LV_RADIUS_CIRCLE;
  dsc.bg_opa = opa;
  dsc.bg_color = lv_color_hex(color);
  lv_area_t area = {cx - r, cy - r, cx + r - 1, cy + r - 1};
  lv_draw_rect(layer, &dsc, &area);
}

void draw_steel_bezel(lv_layer_t* layer, const Geometry& g, float face_frac) {
  // Outer chrome: bright top, dark band below the middle, bright again at
  // the bottom -- the reflection of a polished steel ring.
  {
    const uint32_t c[] = {0xF4F4F4, 0x9C9CA0, 0x34343A, 0xC4C4C8};
    const uint8_t f[] = {0, 80, 170, 255};
    draw_circle_grad(layer, g.cx, g.cy, g.px(0.995f), c, f, 4);
  }
  // Inner bevel: reversed gradient so the ring reads as a raised lip.
  {
    const uint32_t c[] = {0x26262A, 0x707074, 0xE8E8E8};
    const uint8_t f[] = {0, 128, 255};
    draw_circle_grad(layer, g.cx, g.cy, g.px(face_frac + 0.06f), c, f, 3);
  }
  draw_circle(layer, g.cx, g.cy, g.px(face_frac + 0.018f), 0x0C0C0E);
  // Face: lighter upper half with a soft glass reflection edge.
  {
    const uint32_t c[] = {color::kFaceTop, color::kFaceMid, color::kFaceLow,
                          color::kFaceBottom};
    const uint8_t f[] = {0, 118, 132, 255};
    draw_circle_grad(layer, g.cx, g.cy, g.px(face_frac), c, f, 4);
  }
}

void draw_knob(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r) {
  draw_circle(layer, cx, cy + r / 6, r, 0x000000, LV_OPA_40);  // shadow
  draw_circle(layer, cx, cy, r, 0x1A1A1C);
  const uint32_t c[] = {0xFAFAFA, 0xA4A4A8, 0x4C4C50};
  const uint8_t f[] = {0, 120, 255};
  draw_circle_grad(layer, cx, cy, (r * 7) / 8, c, f, 3);
  draw_circle(layer, cx - r / 4, cy - r / 3, r / 3, 0xFFFFFF, LV_OPA_50);
}

void draw_text(lv_layer_t* layer, const char* text, const lv_font_t* font,
               uint32_t color, int32_t cx, int32_t cy, lv_opa_t opa) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.text = text;
  // Draws are queued until the layer is finished; let LVGL copy the text so
  // callers may pass short-lived buffers.
  dsc.text_local = 1;
  dsc.font = font;
  dsc.color = lv_color_hex(color);
  dsc.opa = opa;
  dsc.align = LV_TEXT_ALIGN_CENTER;
  lv_area_t area = {cx - size.x / 2, cy - size.y / 2,
                    cx - size.x / 2 + size.x, cy - size.y / 2 + size.y};
  lv_draw_label(layer, &dsc, &area);
}

TextImage::TextImage(const char* text, const lv_font_t* font,
                     uint32_t color) {
  lv_point_t size;
  lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  if (size.x <= 0 || size.y <= 0) {
    return;
  }
  const int32_t pad = 2;
  buf_ = lv_draw_buf_create(size.x + 2 * pad, size.y + 2 * pad,
                            LV_COLOR_FORMAT_ARGB8888, 0);
  if (buf_ == nullptr) {
    return;
  }
  lv_draw_buf_clear(buf_, nullptr);

  lv_layer_t layer;
  init_offscreen_layer(&layer, buf_);
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.text = text;
  dsc.font = font;
  dsc.color = lv_color_hex(color);
  lv_area_t area = {pad, pad, pad + size.x, pad + size.y};
  lv_draw_label(&layer, &dsc, &area);
  finish_offscreen_layer(&layer);
}

TextImage::~TextImage() {
  if (buf_ != nullptr) {
    lv_image_cache_drop(buf_);
    lv_draw_buf_destroy(buf_);
  }
}

float TextImage::radius() const {
  if (buf_ == nullptr) return 0.0f;
  const float w = buf_->header.w;
  const float h = buf_->header.h;
  return 0.5f * sqrtf(w * w + h * h);
}

void TextImage::draw(lv_layer_t* layer, float cx, float cy,
                     float rotation_deg, lv_opa_t opa) const {
  if (buf_ == nullptr) {
    return;
  }
  const int32_t w = buf_->header.w;
  const int32_t h = buf_->header.h;
  lv_draw_image_dsc_t dsc;
  lv_draw_image_dsc_init(&dsc);
  dsc.src = buf_;
  dsc.rotation = (int32_t)lroundf(rotation_deg * 10.0f);
  dsc.pivot.x = w / 2;
  dsc.pivot.y = h / 2;
  dsc.antialias = 1;
  dsc.opa = opa;
  const int32_t x = (int32_t)lroundf(cx) - w / 2;
  const int32_t y = (int32_t)lroundf(cy) - h / 2;
  lv_area_t area = {x, y, x + w - 1, y + h - 1};
  lv_draw_image(layer, &dsc, &area);
}

void draw_text_on_canvas_rotated(lv_obj_t* canvas, const char* text,
                                 const lv_font_t* font, uint32_t color,
                                 float cx, float cy, float rotation_deg) {
  TextImage image(text, font, color);
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  image.draw(&layer, cx, cy, rotation_deg);
  lv_canvas_finish_layer(canvas, &layer);
}

}  // namespace ui
}  // namespace skgauges
