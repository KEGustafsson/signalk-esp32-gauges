#pragma once

// Drawing helpers shared by the gauge pages.
//
// Everything is sized relative to the round screen's radius R, so the same
// page code renders correctly on the 480x480 (2.1") and 720x720 (4") panels.
//
// Angle convention ("dial angle"): degrees clockwise from 12 o'clock. LVGL's
// arc angles start at 3 o'clock, so to_lv_arc_angle() converts.

#include <lvgl.h>
#include <stdint.h>

#include <vector>

namespace skgauges {
namespace ui {

struct Geometry {
  int32_t cx;
  int32_t cy;
  int32_t r;  // Radius of the round screen

  // Pixels for a fraction of R.
  int32_t px(float frac) const { return (int32_t)(frac * r + 0.5f); }
  float pxf(float frac) const { return frac * r; }
};

// Colours of the classic steel instrument look.
namespace color {
constexpr uint32_t kBackground = 0x000000;
constexpr uint32_t kFaceTop = 0x55555A;
constexpr uint32_t kFaceMid = 0x46464B;
constexpr uint32_t kFaceLow = 0x303034;
constexpr uint32_t kFaceBottom = 0x1E1E21;
constexpr uint32_t kMarking = 0xF2F2F2;
constexpr uint32_t kNeedle = 0xE0141A;
constexpr uint32_t kNeedleDark = 0x8C0A0E;
constexpr uint32_t kLcdTop = 0xC9D1B9;
constexpr uint32_t kLcdBottom = 0x9EA891;
constexpr uint32_t kLcdInk = 0x161A14;
}  // namespace color

lv_point_precise_t polar(const Geometry& g, float dial_angle_deg,
                         float radius_px);
float to_lv_arc_angle(float dial_angle_deg);

// Largest built-in Montserrat font whose size is <= px (min 12).
const lv_font_t* font_for_px(int32_t px);

// A canvas for a static dial face, freed with the object. It starts as
// XRGB8888 so gradients are drawn at full precision; call finish_canvas()
// when drawing is done to convert it to RGB565 with ordered dithering (no
// visible banding on the panel, half the memory).
lv_obj_t* create_canvas(lv_obj_t* parent, int32_t w, int32_t h);
void finish_canvas(lv_obj_t* canvas);

// Filled circle with a vertical gradient (top -> bottom). Up to 4 stops.
void draw_circle_grad(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r,
                      const uint32_t* colors, const uint8_t* fracs,
                      int stops);
void draw_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r,
                 uint32_t color, lv_opa_t opa = LV_OPA_COVER);

// Chrome bezel ring and the dark dial face inside it. face_frac is the face
// radius as a fraction of R.
void draw_steel_bezel(lv_layer_t* layer, const Geometry& g, float face_frac);

// Steel knob: the needle cap and the needle stop pins.
void draw_knob(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r);

// Text centred on (cx, cy).
void draw_text(lv_layer_t* layer, const char* text, const lv_font_t* font,
               uint32_t color, int32_t cx, int32_t cy,
               lv_opa_t opa = LV_OPA_COVER);

// Text pre-rendered into a transparent ARGB8888 buffer so it can be drawn
// rotated (lv_draw_image). Owns its buffer.
class TextImage {
 public:
  TextImage(const char* text, const lv_font_t* font, uint32_t color);
  ~TextImage();
  TextImage(const TextImage&) = delete;
  TextImage& operator=(const TextImage&) = delete;

  // Draw centred on (cx, cy), rotated by rotation_deg (clockwise).
  void draw(lv_layer_t* layer, float cx, float cy, float rotation_deg,
            lv_opa_t opa = LV_OPA_COVER) const;
  bool valid() const { return buf_ != nullptr; }
  // Half the diagonal: no rotation draws outside this radius of the centre.
  float radius() const;

 private:
  lv_draw_buf_t* buf_ = nullptr;
};

// Draws `text` centred on (cx, cy), rotated so it reads along the dial at
// dial_angle_deg (the classic radial-label look), into a canvas. Use for
// static faces only: renders through a temporary buffer.
void draw_text_on_canvas_rotated(lv_obj_t* canvas, const char* text,
                                 const lv_font_t* font, uint32_t color,
                                 float cx, float cy, float rotation_deg);

}  // namespace ui
}  // namespace skgauges
