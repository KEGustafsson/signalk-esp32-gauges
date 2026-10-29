#include "radial_gauge.h"

#include <math.h>
#include <stdio.h>

namespace skgauges {
namespace ui {

namespace {

// Proportions (fractions of the screen radius R), tuned to the reference
// steel gauges.
constexpr float kFace = 0.85f;
constexpr float kTickOuter = 0.80f;
constexpr float kMajorTickLen = 0.10f;
constexpr float kMinorTickLen = 0.05f;
constexpr float kZoneWidth = 0.05f;
constexpr float kLabelRadius = 0.585f;
constexpr float kLabelFont = 0.085f;
constexpr float kTitleY = -0.30f;
constexpr float kTitleFont = 0.10f;
constexpr float kUnitY = -0.165f;
constexpr float kUnitFont = 0.085f;
constexpr float kLcdY = 0.31f;
constexpr float kLcdW = 0.80f;
constexpr float kLcdH = 0.25f;
constexpr float kNeedleTip = 0.77f;
constexpr float kNeedleTail = 0.15f;
constexpr float kNeedleHalfWidth = 0.03f;
constexpr float kCapRadius = 0.085f;
constexpr float kPinRadius = 0.028f;
constexpr float kPinAngleOffset = 9.0f;
constexpr float kPinRadiusPos = 0.70f;

uint8_t lcd_slots(const GaugeDef& def) {
  float span = fmaxf(fabsf(def.min), fabsf(def.max));
  int digits = 1;
  while (span >= 10.0f && digits < 6) {
    span /= 10.0f;
    digits++;
  }
  return (uint8_t)(digits + def.decimals + (def.min < 0.0f ? 1 : 0));
}

void format_label(char* buf, size_t len, float value, float step) {
  if (fabsf(step - roundf(step)) < 0.001f) {
    snprintf(buf, len, "%.0f", value);
  } else {
    snprintf(buf, len, "%.1f", value);
  }
}

}  // namespace

RadialGaugePage::RadialGaugePage(lv_obj_t* parent, const GaugeDef& def,
                                 const Geometry& geometry)
    : GaugePage(parent, def, geometry),
      needle_obj_(nullptr),
      lcd_(root_, geometry.cx, geometry.cy + geometry.px(kLcdY),
           geometry.px(kLcdW), geometry.px(kLcdH), lcd_slots(def)),
      shown_angle_(kStartAngle) {
  draw_face();
  finish_canvas(face_);
  // Created after the LCD so the needle sweeps over it, as on the original.
  needle_obj_ = create_overlay(needle_draw_cb);
}

float RadialGaugePage::value_to_angle(float value) const {
  float range = def_.max - def_.min;
  if (range <= 0.0f) return kStartAngle;
  float t = (value - def_.min) / range;
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return kStartAngle + t * kSweep;
}

void RadialGaugePage::draw_face() {
  lv_layer_t layer;
  lv_canvas_init_layer(face_, &layer);

  draw_steel_bezel(&layer, g_, kFace);

  // Coloured zones along the outer edge of the tick ring.
  for (uint8_t i = 0; i < def_.zone_count && i < kMaxZones; i++) {
    const Zone& z = def_.zones[i];
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center.x = g_.cx;
    arc.center.y = g_.cy;
    arc.radius = (uint16_t)g_.px(kTickOuter);
    arc.width = g_.px(kZoneWidth);
    arc.color = lv_color_hex(z.color);
    arc.start_angle = to_lv_arc_angle(value_to_angle(z.from));
    arc.end_angle = to_lv_arc_angle(value_to_angle(z.to));
    lv_draw_arc(&layer, &arc);
  }

  // Ticks.
  const int majors = (int)lroundf((def_.max - def_.min) / def_.major_step);
  const int minors_per = def_.minor_per_major > 0 ? def_.minor_per_major : 1;
  const int total = majors * minors_per;
  for (int i = 0; i <= total; i++) {
    const bool major = (i % minors_per) == 0;
    const float value =
        def_.min + def_.major_step * (float)i / (float)minors_per;
    const float angle = value_to_angle(value);
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(color::kMarking);
    line.width = major ? LV_MAX(2, g_.px(0.016f)) : LV_MAX(1, g_.px(0.007f));
    line.p1 = polar(g_, angle, g_.pxf(kTickOuter));
    line.p2 = polar(g_, angle,
                    g_.pxf(kTickOuter - (major ? kMajorTickLen
                                               : kMinorTickLen)));
    lv_draw_line(&layer, &line);
  }

  // Title and unit, upright.
  draw_text(&layer, def_.title, font_for_px(g_.px(kTitleFont)),
            color::kMarking, g_.cx, g_.cy + g_.px(kTitleY));
  draw_text(&layer, def_.unit, font_for_px(g_.px(kUnitFont)),
            color::kMarking, g_.cx, g_.cy + g_.px(kUnitY));

  // Needle stop pins just past both ends of the scale.
  const float pin_angles[2] = {kStartAngle - kPinAngleOffset,
                               kStartAngle + kSweep + kPinAngleOffset};
  for (float a : pin_angles) {
    lv_point_precise_t p = polar(g_, a, g_.pxf(kPinRadiusPos));
    draw_knob(&layer, (int32_t)p.x, (int32_t)p.y, g_.px(kPinRadius));
  }

  lv_canvas_finish_layer(face_, &layer);

  // Scale numbers, rotated to read along the dial like the original.
  const lv_font_t* label_font = font_for_px(g_.px(kLabelFont));
  for (int i = 0; i <= majors; i++) {
    const float value = def_.min + def_.major_step * (float)i;
    const float angle = value_to_angle(value);
    char text[12];
    format_label(text, sizeof(text), value, def_.major_step);
    lv_point_precise_t p = polar(g_, angle, g_.pxf(kLabelRadius));
    draw_text_on_canvas_rotated(face_, text, label_font, color::kMarking,
                                p.x, p.y, angle);
  }
}

void RadialGaugePage::needle_area(float angle, lv_area_t* area) const {
  // Bounding box of tip, tail and cap, plus the shadow offset.
  lv_point_precise_t tip = polar(g_, angle, g_.pxf(kNeedleTip));
  lv_point_precise_t tail = polar(g_, angle + 180.0f, g_.pxf(kNeedleTail));
  const float pad = g_.pxf(kCapRadius) + g_.pxf(0.04f) + 2.0f;
  area->x1 = (int32_t)floorf(fminf(fminf(tip.x, tail.x), g_.cx) - pad);
  area->y1 = (int32_t)floorf(fminf(fminf(tip.y, tail.y), g_.cy) - pad);
  area->x2 = (int32_t)ceilf(fmaxf(fmaxf(tip.x, tail.x), g_.cx) + pad);
  area->y2 = (int32_t)ceilf(fmaxf(fmaxf(tip.y, tail.y), g_.cy) + pad);
}

void RadialGaugePage::draw_needle(lv_layer_t* layer, float angle) const {
  const float hw = g_.pxf(kNeedleHalfWidth);
  lv_point_precise_t tip = polar(g_, angle, g_.pxf(kNeedleTip));
  lv_point_precise_t tail = polar(g_, angle + 180.0f, g_.pxf(kNeedleTail));
  lv_point_precise_t left = polar(g_, angle - 90.0f, hw);
  lv_point_precise_t right = polar(g_, angle + 90.0f, hw);
  lv_point_precise_t center = {(lv_value_precise_t)g_.cx,
                               (lv_value_precise_t)g_.cy};

  auto triangle = [layer](lv_point_precise_t a, lv_point_precise_t b,
                          lv_point_precise_t c, uint32_t color,
                          lv_opa_t opa, float dx, float dy) {
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.p[0] = {a.x + dx, a.y + dy};
    dsc.p[1] = {b.x + dx, b.y + dy};
    dsc.p[2] = {c.x + dx, c.y + dy};
    dsc.color = lv_color_hex(color);
    dsc.opa = opa;
    lv_draw_triangle(layer, &dsc);
  };

  // Soft shadow on the face.
  const float sx = g_.pxf(0.015f);
  const float sy = g_.pxf(0.022f);
  triangle(tip, left, right, 0x000000, LV_OPA_40, sx, sy);
  triangle(tail, left, right, 0x000000, LV_OPA_40, sx, sy);

  // Two-tone blade: the lit and the shaded half of a ridged needle.
  triangle(tip, left, center, color::kNeedle, LV_OPA_COVER, 0, 0);
  triangle(tip, center, right, color::kNeedleDark, LV_OPA_COVER, 0, 0);
  triangle(tail, left, center, color::kNeedle, LV_OPA_COVER, 0, 0);
  triangle(tail, center, right, color::kNeedleDark, LV_OPA_COVER, 0, 0);

  draw_knob(layer, g_.cx, g_.cy, g_.px(kCapRadius));
}

void RadialGaugePage::needle_draw_cb(lv_event_t* e) {
  auto* self = static_cast<RadialGaugePage*>(lv_event_get_user_data(e));
  self->draw_needle(lv_event_get_layer(e), self->shown_angle_);
}

void RadialGaugePage::update(const GaugeValue& value, uint32_t now_ms) {
  const bool fresh = value.fresh(now_ms);
  if (fresh) {
    lcd_.set_value(value.value, def_.decimals);
  } else {
    lcd_.set_dashes();
  }

  const float target = value_to_angle(fresh ? value.value : def_.min);
  lv_area_t before;
  needle_area(shown_angle_, &before);
  if (smooth_toward(&shown_angle_, target, 0.25f, 0.05f)) {
    lv_area_t after;
    needle_area(shown_angle_, &after);
    lv_obj_invalidate_area(needle_obj_, &before);
    lv_obj_invalidate_area(needle_obj_, &after);
  }
}

}  // namespace ui
}  // namespace skgauges
