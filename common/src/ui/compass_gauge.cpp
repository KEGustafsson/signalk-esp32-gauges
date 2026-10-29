#include "compass_gauge.h"

#include <math.h>
#include <stdio.h>

namespace skgauges {
namespace ui {

namespace {

constexpr float kFace = 0.85f;
constexpr float kCardOuter = 0.82f;
constexpr float kTick2Len = 0.035f;
constexpr float kTick10Len = 0.075f;
constexpr float kNumberRadius = 0.66f;
constexpr float kNumberFont = 0.09f;
constexpr float kCardinalRadius = 0.645f;
constexpr float kCardinalFont = 0.15f;
constexpr float kInterRadius = 0.47f;
constexpr float kInterFont = 0.07f;
constexpr float kLubberTip = 0.71f;
constexpr float kLubberBase = 0.86f;
constexpr float kLubberHalf = 0.045f;
constexpr float kTitleY = -0.12f;
constexpr float kTitleFont = 0.075f;
constexpr float kLcdY = 0.28f;
constexpr float kLcdW = 0.74f;
constexpr float kLcdH = 0.23f;
constexpr uint32_t kNorthRed = 0xE01418;

}  // namespace

CompassGaugePage::CompassGaugePage(lv_obj_t* parent, const GaugeDef& def,
                                   const Geometry& geometry)
    : GaugePage(parent, def, geometry),
      card_obj_(nullptr),
      lcd_(root_, geometry.cx, geometry.cy + geometry.px(kLcdY),
           geometry.px(kLcdW), geometry.px(kLcdH), 3),
      shown_heading_(NAN) {
  draw_face();
  finish_canvas(face_);

  // Pre-render the card's texts once; they are drawn rotated every frame.
  const lv_font_t* number_font = font_for_px(g_.px(kNumberFont));
  const lv_font_t* cardinal_font = font_for_px(g_.px(kCardinalFont));
  const lv_font_t* inter_font = font_for_px(g_.px(kInterFont));
  const char* const cardinals[4] = {"N", "E", "S", "W"};
  const char* const inters[4] = {"NE", "SE", "SW", "NW"};
  for (int b = 0; b < 360; b += 30) {
    CardLabel label;
    label.bearing = (float)b;
    if (b % 90 == 0) {
      label.radius_frac = kCardinalRadius;
      label.image.reset(new TextImage(cardinals[b / 90], cardinal_font,
                                      b == 0 ? kNorthRed : color::kMarking));
    } else {
      char text[8];
      snprintf(text, sizeof(text), "%d", b);
      label.radius_frac = kNumberRadius;
      label.image.reset(new TextImage(text, number_font, color::kMarking));
    }
    labels_.push_back(std::move(label));
  }
  for (int i = 0; i < 4; i++) {
    CardLabel label;
    label.bearing = 45.0f + 90.0f * i;
    label.radius_frac = kInterRadius;
    label.image.reset(new TextImage(inters[i], inter_font, 0xBDBDBD));
    labels_.push_back(std::move(label));
  }

  // Stacking: face canvas, card, LCD. The LCD already exists (member init),
  // so slot the card in directly above the canvas.
  card_obj_ = create_overlay(card_draw_cb);
  lv_obj_move_to_index(card_obj_, 1);
  lcd_.set_prefix(def.unit);
}

void CompassGaugePage::draw_face() {
  lv_layer_t layer;
  lv_canvas_init_layer(face_, &layer);
  draw_steel_bezel(&layer, g_, kFace);
  draw_text(&layer, def_.title, font_for_px(g_.px(kTitleFont)), 0xDADADA,
            g_.cx, g_.cy + g_.px(kTitleY));
  lv_canvas_finish_layer(face_, &layer);
}

void CompassGaugePage::draw_card(lv_layer_t* layer) const {
  // Before the first sample the card rests at north.
  const float heading = isnan(shown_heading_) ? 0.0f : shown_heading_;

  // Graduations every 2 degrees, longer and heavier every 10.
  for (int b = 0; b < 360; b += 2) {
    const bool ten = (b % 10) == 0;
    const float angle = (float)b - heading;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(color::kMarking);
    line.width = ten ? LV_MAX(2, g_.px(0.014f)) : LV_MAX(1, g_.px(0.006f));
    line.p1 = polar(g_, angle, g_.pxf(kCardOuter));
    line.p2 = polar(g_, angle, g_.pxf(kCardOuter - (ten ? kTick10Len
                                                         : kTick2Len)));
    lv_draw_line(layer, &line);
  }

  for (const CardLabel& label : labels_) {
    const float angle = label.bearing - heading;
    lv_point_precise_t p = polar(g_, angle, g_.pxf(label.radius_frac));
    label.image->draw(layer, p.x, p.y, angle);
  }

  // Centre ring.
  lv_draw_arc_dsc_t ring;
  lv_draw_arc_dsc_init(&ring);
  ring.center.x = g_.cx;
  ring.center.y = g_.cy;
  ring.radius = (uint16_t)g_.px(0.05f);
  ring.width = LV_MAX(2, g_.px(0.008f));
  ring.start_angle = 0;
  ring.end_angle = 360;
  ring.color = lv_color_hex(0xCFCFCF);
  lv_draw_arc(layer, &ring);

  // Fixed red lubber mark at 12 o'clock, over the card.
  lv_draw_triangle_dsc_t lubber;
  lv_draw_triangle_dsc_init(&lubber);
  lubber.p[0] = {(lv_value_precise_t)g_.cx,
                 (lv_value_precise_t)(g_.cy - g_.pxf(kLubberTip))};
  lubber.p[1] = {g_.cx - g_.pxf(kLubberHalf),
                 (lv_value_precise_t)(g_.cy - g_.pxf(kLubberBase))};
  lubber.p[2] = {g_.cx + g_.pxf(kLubberHalf),
                 (lv_value_precise_t)(g_.cy - g_.pxf(kLubberBase))};
  lubber.color = lv_color_hex(kNorthRed);
  lv_draw_triangle(layer, &lubber);
}

void CompassGaugePage::card_draw_cb(lv_event_t* e) {
  auto* self = static_cast<CompassGaugePage*>(lv_event_get_user_data(e));
  self->draw_card(lv_event_get_layer(e));
}

void CompassGaugePage::invalidate_card() {
  const int32_t r = g_.px(kLubberBase) + 2;
  lv_area_t area = {g_.cx - r, g_.cy - r, g_.cx + r, g_.cy + r};
  lv_obj_invalidate_area(card_obj_, &area);
}

void CompassGaugePage::update(const GaugeValue& value, uint32_t now_ms) {
  const bool fresh = value.fresh(now_ms);
  if (!fresh) {
    lcd_.set_dashes();
    return;  // The card stays where it was
  }
  lcd_.set_value((float)(lroundf(value.value) % 360), 0, true);
  if (smooth_toward(&shown_heading_, value.value, 0.2f, 0.1f, true)) {
    invalidate_card();
  }
}

}  // namespace ui
}  // namespace skgauges
