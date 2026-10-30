#include "attitude_gauge.h"

#include <math.h>
#include <stdio.h>

namespace skgauges {
namespace ui {

namespace {

constexpr float kFace = 0.85f;
constexpr float kDegToRad = 0.017453292519943295f;

// Heel scale on the upper arc. The scale is stretched a little so +-40 deg
// heel covers +-48 deg of dial; 1.2 keeps the 10-degree zone edges on whole
// dial degrees (LVGL draws arcs at integer angles).
constexpr float kScaleStretch = 1.2f;
constexpr float kTickOuter = 0.80f;
constexpr float kMajorTickLen = 0.08f;
constexpr float kMinorTickLen = 0.045f;
constexpr float kZoneWidth = 0.045f;
constexpr float kLabelRadius = 0.665f;
constexpr float kLabelFont = 0.065f;
constexpr float kSideLabelAngle = 61.0f;
constexpr float kSideLabelFont = 0.05f;
constexpr float kPointerBase = 0.865f;
constexpr float kPointerTip = 0.765f;
constexpr float kPointerHalf = 0.04f;

// Horizon window. The horizon is fixed and level; the boat symbol moves.
constexpr float kDiscY = -0.02f;
constexpr float kDiscRadius = 0.50f;
constexpr float kRimWidth = 0.035f;
constexpr float kPitchPxPerDeg = 0.022f;  // Fraction of R per degree of trim
constexpr float kMaxPitchShown = 12.0f;   // Keeps the whole boat in the window
// Boat symbol seen from astern, in fractions of R around its waterline
// centre: hull trapezoid, deck, and a mast that makes the heel obvious.
constexpr float kHullHalfTop = 0.20f;
constexpr float kHullHalfBottom = 0.10f;
constexpr float kFreeboard = 0.035f;  // Waterline to deck
constexpr float kDraft = 0.03f;       // Waterline to keel
constexpr float kMastHeight = 0.17f;  // Deck to masthead
constexpr uint32_t kSkyTop = 0x2C6AA6;
constexpr uint32_t kSkyHorizon = 0x9CCBEA;
constexpr uint32_t kSeaTop = 0x1E5E72;
constexpr uint32_t kSeaBottom = 0x0A2630;
constexpr uint32_t kBoatSymbol = 0xFF8C1A;

constexpr float kHeelLcdY = 0.42f;
constexpr float kHeelLcdW = 0.66f;
constexpr float kHeelLcdH = 0.17f;
constexpr float kTrimLcdY = 0.615f;
constexpr float kTrimLcdW = 0.46f;
constexpr float kTrimLcdH = 0.105f;

lv_color_t lerp_color(uint32_t a, uint32_t b, float t) {
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return lv_color_mix(lv_color_hex(b), lv_color_hex(a),
                      (lv_opa_t)lroundf(t * 255.0f));
}

void draw_line(lv_layer_t* layer, float x1, float y1, float x2, float y2,
               int32_t width, uint32_t color, lv_opa_t opa = LV_OPA_COVER) {
  lv_draw_line_dsc_t dsc;
  lv_draw_line_dsc_init(&dsc);
  dsc.p1 = {x1, y1};
  dsc.p2 = {x2, y2};
  dsc.width = width;
  dsc.color = lv_color_hex(color);
  dsc.opa = opa;
  dsc.round_start = 1;
  dsc.round_end = 1;
  lv_draw_line(layer, &dsc);
}

}  // namespace

AttitudeGaugePage::AttitudeGaugePage(lv_obj_t* parent, const GaugeDef& def,
                                     const Geometry& geometry)
    : GaugePage(parent, def, geometry),
      overlay_(nullptr),
      heel_lcd_(root_, geometry.cx, geometry.cy + geometry.px(kHeelLcdY),
                geometry.px(kHeelLcdW), geometry.px(kHeelLcdH), 3),
      trim_lcd_(root_, geometry.cx, geometry.cy + geometry.px(kTrimLcdY),
                geometry.px(kTrimLcdW), geometry.px(kTrimLcdH), 4),
      shown_roll_(0.0f),
      shown_pitch_(0.0f) {
  draw_face();
  finish_canvas(face_);
  trim_lcd_.set_prefix("TRIM");
  trim_lcd_.set_suffix("\xC2\xB0");
  heel_lcd_.set_suffix("\xC2\xB0");

  // Stacking: face canvas (with the horizon), boat/pointer overlay, then the
  // LCDs over the window.
  overlay_ = create_overlay(draw_cb);
  lv_obj_move_to_index(overlay_, 1);
  set_stale_look_layered(overlay_, true);  // Until the first attitude arrives
}

float AttitudeGaugePage::heel_to_angle(float heel_deg) const {
  return heel_deg * kScaleStretch;
}

void AttitudeGaugePage::draw_face() {
  lv_layer_t layer;
  lv_canvas_init_layer(face_, &layer);
  draw_steel_bezel(&layer, g_, kFace);

  // Bands: a zone starting below zero spans both sides already; the others
  // are mirrored to port.
  for (uint8_t i = 0; i < def_.zone_count && i < kMaxZones; i++) {
    const Zone& z = def_.zones[i];
    const int sides = z.from < 0.0f ? 1 : 2;
    for (int side = 0; side < sides; side++) {
      float from = side == 0 ? z.from : -z.to;
      float to = side == 0 ? z.to : -z.from;
      lv_draw_arc_dsc_t arc;
      lv_draw_arc_dsc_init(&arc);
      arc.center.x = g_.cx;
      arc.center.y = g_.cy;
      arc.radius = (uint16_t)g_.px(kTickOuter);
      arc.width = g_.px(kZoneWidth);
      arc.color = lv_color_hex(z.color);
      arc.start_angle = to_lv_arc_angle(heel_to_angle(from));
      arc.end_angle = to_lv_arc_angle(heel_to_angle(to));
      lv_draw_arc(&layer, &arc);
    }
  }

  // Ticks every 5 degrees, major every 10.
  const float step = def_.major_step / LV_MAX(1, def_.minor_per_major);
  for (float heel = def_.min; heel <= def_.max + 0.01f; heel += step) {
    const bool major = fabsf(fmodf(heel, def_.major_step)) < 0.01f;
    const float angle = heel_to_angle(heel);
    lv_point_precise_t a = polar(g_, angle, g_.pxf(kTickOuter));
    lv_point_precise_t b = polar(
        g_, angle,
        g_.pxf(kTickOuter - (major ? kMajorTickLen : kMinorTickLen)));
    draw_line(&layer, a.x, a.y, b.x, b.y,
              major ? LV_MAX(2, g_.px(0.014f)) : LV_MAX(1, g_.px(0.007f)),
              color::kMarking);
  }

  // The sea horizon is part of the static face: it never moves.
  draw_window(&layer);
  lv_canvas_finish_layer(face_, &layer);

  // Scale numbers (absolute values on both sides), rotated along the arc.
  const lv_font_t* font = font_for_px(g_.px(kLabelFont));
  for (float heel = def_.major_step; heel <= def_.max + 0.01f;
       heel += def_.major_step) {
    char text[8];
    snprintf(text, sizeof(text), "%.0f", heel);
    for (int sign = -1; sign <= 1; sign += 2) {
      const float angle = heel_to_angle(sign * heel);
      lv_point_precise_t p = polar(g_, angle, g_.pxf(kLabelRadius));
      draw_text_on_canvas_rotated(face_, text, font, color::kMarking, p.x,
                                  p.y, angle);
    }
  }
  const lv_font_t* side_font = font_for_px(g_.px(kSideLabelFont));
  lv_point_precise_t port = polar(g_, -kSideLabelAngle, g_.pxf(0.72f));
  lv_point_precise_t stbd = polar(g_, kSideLabelAngle, g_.pxf(0.72f));
  draw_text_on_canvas_rotated(face_, "PORT", side_font, 0xFF5A5A, port.x,
                              port.y, -kSideLabelAngle);
  draw_text_on_canvas_rotated(face_, "STBD", side_font, 0x4CD964, stbd.x,
                              stbd.y, kSideLabelAngle);
}

void AttitudeGaugePage::draw_window(lv_layer_t* layer) const {
  const float hx = (float)g_.cx;
  const float hy = g_.cy + g_.pxf(kDiscY);
  const float rd = g_.pxf(kDiscRadius);
  const float px_per_deg = g_.pxf(kPitchPxPerDeg);

  // Sky: the whole disc.
  {
    const uint32_t c[] = {kSkyTop, kSkyHorizon};
    const uint8_t f[] = {0, 255};
    draw_circle_grad(layer, (int32_t)hx, (int32_t)hy, (int32_t)rd, c, f, 2);
  }

  // Sea: the lower half, one span per pixel row so it stays inside the disc.
  lv_draw_rect_dsc_t row;
  lv_draw_rect_dsc_init(&row);
  row.bg_opa = LV_OPA_COVER;
  for (int32_t y = (int32_t)lroundf(hy); y <= (int32_t)floorf(hy + rd); y++) {
    const float dy = (float)y + 0.5f - hy;
    const float half = sqrtf(fmaxf(0.0f, rd * rd - dy * dy));
    if (half < 0.5f) continue;
    row.bg_color = lerp_color(kSeaTop, kSeaBottom, dy / rd);
    lv_area_t a = {(int32_t)lroundf(hx - half), y,
                   (int32_t)lroundf(hx + half) - 1, y};
    lv_draw_rect(layer, &row, &a);
  }

  // Horizon line.
  draw_line(layer, hx - rd + 1.0f, hy, hx + rd - 1.0f, hy,
            LV_MAX(2, g_.px(0.008f)), 0xFFFFFF, LV_OPA_80);

  // Trim ladder at +-5 and +-10 degrees (bow up above the horizon).
  const lv_font_t* font = font_for_px(g_.px(0.05f));
  const float marks[4] = {-10.0f, -5.0f, 5.0f, 10.0f};
  for (float m : marks) {
    const float y = hy - m * px_per_deg;
    const float len = g_.pxf(fabsf(m) >= 10.0f ? 0.10f : 0.06f);
    draw_line(layer, hx - len, y, hx + len, y, LV_MAX(1, g_.px(0.006f)),
              0xFFFFFF, LV_OPA_70);
    const char* text = fabsf(m) >= 10.0f ? "10" : "5";
    const float off = len + g_.pxf(0.045f);
    draw_text(layer, text, font, 0xFFFFFF, (int32_t)(hx - off), (int32_t)y,
              LV_OPA_80);
    draw_text(layer, text, font, 0xFFFFFF, (int32_t)(hx + off), (int32_t)y,
              LV_OPA_80);
  }

  // Rim: covers the jagged disc edge and frames the window.
  lv_draw_arc_dsc_t rim;
  lv_draw_arc_dsc_init(&rim);
  rim.center.x = (int32_t)hx;
  rim.center.y = (int32_t)hy;
  rim.radius = (uint16_t)(rd + g_.pxf(kRimWidth) - 1.0f);
  rim.width = g_.px(kRimWidth) + 1;
  rim.start_angle = 0;
  rim.end_angle = 360;
  rim.color = lv_color_hex(0x141416);
  lv_draw_arc(layer, &rim);
  rim.radius = (uint16_t)(rd + g_.pxf(kRimWidth));
  rim.width = LV_MAX(2, g_.px(0.008f));
  rim.color = lv_color_hex(0x9A9AA0);
  lv_draw_arc(layer, &rim);
}

void AttitudeGaugePage::draw_boat(lv_layer_t* layer) const {
  // Seen from astern: heeling to starboard (roll +) turns the boat
  // clockwise; bow up (pitch +) lifts it against the level horizon.
  float pitch = shown_pitch_;
  if (pitch > kMaxPitchShown) pitch = kMaxPitchShown;
  if (pitch < -kMaxPitchShown) pitch = -kMaxPitchShown;
  const float ox = (float)g_.cx;
  const float oy = g_.cy + g_.pxf(kDiscY) - pitch * g_.pxf(kPitchPxPerDeg);
  // Past 90 degrees the symbol says nothing more (and a garbage roll the
  // LCD already shows as EEE must not spin it).
  float roll = shown_roll_;
  if (roll > 90.0f) roll = 90.0f;
  if (roll < -90.0f) roll = -90.0f;
  const float theta = roll * kDegToRad;
  const float c = cosf(theta), s = sinf(theta);
  auto at = [&](float x_frac, float y_frac) {
    const float x = g_.pxf(x_frac), y = g_.pxf(y_frac);
    lv_point_precise_t p;
    p.x = ox + x * c - y * s;
    p.y = oy + x * s + y * c;
    return p;
  };

  // Hull: deck corners up, keel corners down.
  const lv_point_precise_t deck_l = at(-kHullHalfTop, -kFreeboard);
  const lv_point_precise_t deck_r = at(kHullHalfTop, -kFreeboard);
  const lv_point_precise_t keel_l = at(-kHullHalfBottom, kDraft);
  const lv_point_precise_t keel_r = at(kHullHalfBottom, kDraft);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = lv_color_hex(kBoatSymbol);
  tri.p[0] = deck_l;
  tri.p[1] = deck_r;
  tri.p[2] = keel_r;
  lv_draw_triangle(layer, &tri);
  tri.p[0] = deck_l;
  tri.p[1] = keel_r;
  tri.p[2] = keel_l;
  lv_draw_triangle(layer, &tri);
  // Both triangles antialias their shared edge, which would leave a faint
  // seam of sky through the hull.
  draw_line(layer, deck_l.x, deck_l.y, keel_r.x, keel_r.y, 2, kBoatSymbol);

  // Deck line and mast.
  const lv_point_precise_t mast_foot = at(0.0f, -kFreeboard);
  const lv_point_precise_t mast_head = at(0.0f, -kFreeboard - kMastHeight);
  draw_line(layer, deck_l.x, deck_l.y, deck_r.x, deck_r.y,
            LV_MAX(2, g_.px(0.012f)), 0xFFD9A8);
  draw_line(layer, mast_foot.x, mast_foot.y, mast_head.x, mast_head.y,
            LV_MAX(2, g_.px(0.014f)), kBoatSymbol);
}

void AttitudeGaugePage::draw_dynamic(lv_layer_t* layer) const {
  draw_boat(layer);

  // Heel pointer on the scale, toward the low side.
  float heel = shown_roll_;
  if (heel > def_.max) heel = def_.max;
  if (heel < def_.min) heel = def_.min;
  const float angle = heel_to_angle(heel);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.p[0] = polar(g_, angle, g_.pxf(kPointerTip));
  const float half_deg = (kPointerHalf / kPointerBase) * 57.2958f;
  tri.p[1] = polar(g_, angle - half_deg, g_.pxf(kPointerBase));
  tri.p[2] = polar(g_, angle + half_deg, g_.pxf(kPointerBase));
  tri.color = lv_color_hex(color::kNeedle);
  lv_draw_triangle(layer, &tri);
}

void AttitudeGaugePage::draw_cb(lv_event_t* e) {
  auto* self = static_cast<AttitudeGaugePage*>(lv_event_get_user_data(e));
  self->draw_dynamic(lv_event_get_layer(e));
}

void AttitudeGaugePage::invalidate_dynamic() {
  const int32_t r = g_.px(kPointerBase) + 3;
  const int32_t bottom =
      g_.cy + g_.px(kDiscY) + g_.px(kDiscRadius + kRimWidth) + 3;
  lv_area_t area = {g_.cx - r, g_.cy - r, g_.cx + r, bottom};
  lv_obj_invalidate_area(overlay_, &area);
}

void AttitudeGaugePage::update(const GaugeValue& value, uint32_t now_ms) {
  const bool fresh = value.fresh(now_ms);
  // Without live data the boat levels off, dimmed, so it cannot pass for a
  // real "level" reading; the LCDs show dashes.
  set_stale_look_layered(overlay_, !fresh);
  const float roll = fresh ? value.value : 0.0f;
  // A heel-only source sends no pitch: the boat then sits on the waterline
  // and TRIM shows dashes.
  const bool has_pitch = fresh && isfinite(value.value2);
  const float pitch = has_pitch ? value.value2 : 0.0f;

  if (fresh) {
    heel_lcd_.set_value(fabsf(roll), def_.decimals);
    if (fabsf(roll) < 0.05f) {
      heel_lcd_.set_suffix("\xC2\xB0");
    } else {
      heel_lcd_.set_suffix(roll < 0.0f ? "\xC2\xB0 PORT" : "\xC2\xB0 STBD");
    }
    if (has_pitch) {
      trim_lcd_.set_value(pitch, 1);
    } else {
      trim_lcd_.set_dashes();
    }
  } else {
    heel_lcd_.set_dashes();
    heel_lcd_.set_suffix("\xC2\xB0");
    trim_lcd_.set_dashes();
  }

  bool moved = smooth_toward(&shown_roll_, roll, 0.3f, 0.05f);
  moved = smooth_toward(&shown_pitch_, pitch, 0.3f, 0.05f) || moved;
  if (moved) {
    invalidate_dynamic();
  }
}

}  // namespace ui
}  // namespace skgauges
