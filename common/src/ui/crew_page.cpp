#include "crew_page.h"

#include <math.h>
#include <stdio.h>

#include <string>

namespace skgauges {
namespace ui {

namespace {

// Proportions (fractions of R).
constexpr float kFace = 0.85f;
constexpr float kRing = 0.70f;
constexpr float kRingSpanDeg = 118.0f;  // Members from -118 to +118 degrees
constexpr float kMaxStepDeg = 52.0f;
constexpr float kPip = 0.045f;
constexpr float kNameDy = 0.085f;  // Name under its pip, upright
constexpr float kNameFont = 0.052f;
constexpr float kBarsDy = 0.055f;  // Bars under the name
constexpr float kBarsH = 0.045f;
constexpr float kTitleY = -0.30f;
constexpr float kTitleFont = 0.085f;
constexpr float kLcdY = -0.04f;
constexpr float kLcdW = 0.46f;
constexpr float kLcdH = 0.22f;
constexpr float kStatusY = 0.20f;
constexpr float kStatusFont = 0.058f;
constexpr float kVolRadius = 0.79f;
constexpr float kVolWidth = 0.024f;
constexpr float kVolFrom = 148.0f;  // Dial angles: lower right ...
constexpr float kVolTo = 212.0f;    // ... to lower left
constexpr float kVolLabelY = 0.635f;
constexpr float kVolFont = 0.05f;
// Station: the talk disc in the middle.
// The channel name is engraved in the bezel's bright top instead.
constexpr float kEngraveRadius = 0.952f;  // On the bright chrome, outside the dark bevel
constexpr float kEngraveFont = 0.06f;
constexpr uint32_t kEngraveInk = 0x2A2A2E;
constexpr float kStationCountY = -0.355f;
constexpr float kDiscY = 0.0f;
constexpr float kDisc = 0.245f;
constexpr float kStationStatusY = 0.34f;

void dial_point(const Geometry& g, float angle, float radius_frac, float* x,
                float* y) {
  const lv_point_precise_t p = polar(g, angle, g.pxf(radius_frac));
  *x = p.x;
  *y = p.y;
}

}  // namespace

CrewPage::CrewPage(lv_obj_t* parent, const Geometry& geometry, bool station)
    : g_(geometry), station_(station), owns_root_(true) {
  root_ = lv_obj_create(parent);
  lv_obj_remove_style_all(root_);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
  face_ = create_canvas(root_, lv_obj_get_width(parent),
                        lv_obj_get_height(parent));
  build();
}

CrewPage::CrewPage(lv_obj_t* root, lv_obj_t* face, const Geometry& geometry,
                   bool station)
    : g_(geometry), station_(station), root_(root), face_(face),
      owns_root_(false) {
  build();
}

void CrewPage::build() {
  {
    lv_layer_t layer;
    lv_canvas_init_layer(face_, &layer);
    draw_steel_bezel(&layer, g_, kFace);
    // The crew ring, open at the bottom where the volume sits.
    draw_arc_band(&layer, g_, kRing + 0.004f, 0.008f, 0x77777D, LV_OPA_COVER,
                  -kRingSpanDeg - 6.0f, kRingSpanDeg + 6.0f);
    lv_canvas_finish_layer(face_, &layer);
  }
  finish_canvas(face_);
  if (!station_) {
    lcd_.reset(new LcdReadout(root_, g_.cx, g_.cy + g_.px(kLcdY),
                              g_.px(kLcdW), g_.px(kLcdH), 2));
    lcd_->set_suffix("CREW");
  }
  dynamic_ = lv_obj_create(root_);
  lv_obj_remove_style_all(dynamic_);
  lv_obj_remove_flag(dynamic_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(dynamic_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dynamic_, lv_pct(100), lv_pct(100));
  lv_obj_add_event_cb(dynamic_, draw_cb, LV_EVENT_DRAW_MAIN, this);
}

CrewPage::~CrewPage() {
  if (owns_root_) lv_obj_delete(root_);
}

CrewGaugePage::CrewGaugePage(lv_obj_t* parent, const GaugeDef& def,
                             const Geometry& geometry)
    : GaugePage(parent, def, geometry),
      page_(root_, face_, geometry, false) {}

void CrewGaugePage::update(const GaugeValue&, uint32_t now_ms) {
  if (!first_ && (uint32_t)(now_ms - polled_ms_) < kPollMs) return;
  polled_ms_ = now_ms;
  const CrewView v = read_crew_view();
  if (first_ || v != last_) {
    page_.set(v);
    last_ = v;
    first_ = false;
  }
}

void CrewPage::set(const CrewView& view) {
  if (station_ && (title_.empty() || view.channel != view_.channel)) {
    title_ = ArcText(view.channel, font_for_px(g_.px(kEngraveFont)),
                     kEngraveInk, g_.pxf(kEngraveRadius), 100.0f);
  }
  view_ = view;
  if (lcd_) {
    if (view.state == RadioState::kOnChannel) {
      lcd_->set_value((float)view.members.size(), 0);
    } else {
      lcd_->set_dashes();  // No count that cannot be confirmed
    }
  }
  lv_obj_invalidate(dynamic_);
}

void CrewPage::draw_cb(lv_event_t* e) {
  const CrewPage* self = static_cast<const CrewPage*>(lv_event_get_user_data(e));
  self->draw(lv_event_get_layer(e));
}

void CrewPage::draw_member(lv_layer_t* layer, const CrewMember& m,
                           float angle) const {
  float x, y;
  dial_point(g_, angle, kRing, &x, &y);
  const int32_t r = g_.px(kPip);
  const bool on = view_.state == RadioState::kOnChannel;
  const lv_opa_t opa = on ? LV_OPA_COVER : LV_OPA_40;
  if (m.talking && on) {
    draw_circle(layer, (int32_t)x, (int32_t)y, (r * 21) / 10,
                crew_color::kTalk, LV_OPA_20);
    draw_circle(layer, (int32_t)x, (int32_t)y, (r * 3) / 2, crew_color::kTalk,
                LV_OPA_40);
  }
  draw_circle(layer, (int32_t)x, (int32_t)y, r + 2, 0x0C0C0E, opa);
  const uint32_t fill = m.talking && on ? crew_color::kTalk : 0xE6E6E6;
  if (m.speaker) {
    // A node with no person behind it (the Signal K plugin): a speaker.
    draw_circle(layer, (int32_t)x, (int32_t)y, r, 0x2A2A2E, opa);
    draw_text(layer, LV_SYMBOL_VOLUME_MAX, font_for_px(g_.px(0.05f)), fill,
              (int32_t)x, (int32_t)y, opa);
  } else if (m.relayed || m.reticulum) {
    // Heard through a relay or over Reticulum: a ring, not a disc.
    draw_circle(layer, (int32_t)x, (int32_t)y, r, fill, opa);
    draw_circle(layer, (int32_t)x, (int32_t)y, (r * 6) / 10, 0x2A2A2E, opa);
    if (m.reticulum) {
      draw_circle(layer, (int32_t)x, (int32_t)y, r / 4, fill, opa);
    }
  } else {
    const uint32_t c[] = {0xFFFFFF, fill, (fill & 0xFEFEFE) >> 1};
    const uint8_t f[] = {0, 110, 255};
    draw_circle_grad(layer, (int32_t)x, (int32_t)y, r, c, f, 3);
  }
  const float nx = x;
  const float ny = y + g_.pxf(kNameDy);
  std::string name = m.name;
  if (m.reticulum) name += " (R)";
  draw_text(layer, name.c_str(), font_for_px(g_.px(kNameFont)),
            m.talking && on ? crew_color::kTalk : color::kMarking,
            (int32_t)nx, (int32_t)ny, opa);
  draw_bars(layer, nx, ny + g_.pxf(kBarsDy), g_.pxf(kBarsH), on ? m.bars : 0,
            opa);
}

void CrewPage::draw_volume(lv_layer_t* layer) const {
  const bool on = view_.state == RadioState::kOnChannel;
  draw_arc_band(layer, g_, kVolRadius, kVolWidth, 0x18181B, LV_OPA_COVER,
                kVolFrom, kVolTo);
  if (view_.volume_max > 0 && view_.volume > 0) {
    const float t = (float)view_.volume / view_.volume_max;
    const uint32_t c =
        view_.muted || !on ? crew_color::kUnlit : color::kMarking;
    // Fills from the left (212 degrees) towards the right (148).
    draw_arc_band(layer, g_, kVolRadius, kVolWidth, c, LV_OPA_COVER,
                  kVolTo - t * (kVolTo - kVolFrom), kVolTo);
  }
  const lv_font_t* small = font_for_px(g_.px(kVolFont));
  float x, y;
  dial_point(g_, kVolTo + 7.0f, kVolRadius - kVolWidth / 2, &x, &y);
  draw_text(layer, LV_SYMBOL_MINUS, small, crew_color::kQuiet, (int32_t)x,
            (int32_t)y);
  dial_point(g_, kVolFrom - 7.0f, kVolRadius - kVolWidth / 2, &x, &y);
  draw_text(layer, LV_SYMBOL_PLUS, small, crew_color::kQuiet, (int32_t)x,
            (int32_t)y);
  char label[32];
  if (view_.muted) {
    snprintf(label, sizeof(label), LV_SYMBOL_MUTE " MUTED");
  } else {
    snprintf(label, sizeof(label), "VOL %u", (unsigned)view_.volume);
  }
  draw_text(layer, label, small,
            view_.muted ? crew_color::kTransmit : crew_color::kQuiet, g_.cx,
            g_.cy + g_.px(kVolLabelY));
}

void CrewPage::draw_disc(lv_layer_t* layer) const {
  const bool on = view_.state == RadioState::kOnChannel;
  const lv_opa_t opa = (lv_opa_t)(on ? 255 : 140);  // 0.55 off channel, as the app
  const int32_t cx = g_.cx;
  const int32_t cy = g_.cy + g_.px(kDiscY);
  const int32_t r = g_.px(kDisc);
  draw_circle(layer, cx, cy + r / 12, r + 2, 0x000000, LV_OPA_50);  // shadow
  {
    // Chrome rim, like the bezel.
    const uint32_t c[] = {0xF4F4F4, 0x9C9CA0, 0x34343A, 0xC4C4C8};
    const uint8_t f[] = {0, 80, 170, 255};
    draw_circle_grad(layer, cx, cy, r, c, f, 4);
  }
  const int32_t inner = (r * 86) / 100;
  if (view_.transmitting && on) {
    const uint32_t c[] = {0xFF4A4F, color::kNeedle, color::kNeedleDark};
    const uint8_t f[] = {0, 120, 255};
    draw_circle_grad(layer, cx, cy, inner, c, f, 3);
  } else {
    const uint32_t c[] = {0x4A4A50, 0x2E2E33, 0x1C1C1F};
    const uint8_t f[] = {0, 120, 255};
    draw_circle_grad(layer, cx, cy, inner, c, f, 3);
  }
  draw_circle(layer, cx - inner / 3, cy - inner / 2, inner / 3, 0xFFFFFF,
              LV_OPA_10);  // glass highlight
  const lv_font_t* big = font_for_px(g_.px(0.11f));
  const lv_font_t* small = font_for_px(g_.px(0.05f));
  if (view_.transmitting && on) {
    draw_text(layer, "TALKING", font_for_px(g_.px(0.075f)), 0xFFFFFF, cx, cy);
  } else {
    draw_text(layer, "TALK", big, color::kMarking, cx, cy - g_.px(0.02f), opa);
    draw_text(layer, "HOLD", small, crew_color::kQuiet, cx,
              cy + g_.px(0.085f), opa);
  }
}

void CrewPage::draw(lv_layer_t* layer) const {
  // Members around the ring, top-centred, evenly spaced.
  const size_t n = view_.members.size();
  if (n > 0) {
    const float step =
        n > 1 ? fminf(kMaxStepDeg, 2.0f * kRingSpanDeg / (float)(n - 1)) : 0;
    const float first = -step * (float)(n - 1) / 2.0f;
    for (size_t i = 0; i < n; i++) {
      draw_member(layer, view_.members[i], first + step * (float)i);
    }
  }
  const std::string status = crew_status_text(view_);
  const uint32_t status_color = crew_status_color(view_);
  if (station_) {
    title_.draw(layer, g_, 0.0f, false);
    char count[24];
    if (view_.state == RadioState::kOnChannel) {
      snprintf(count, sizeof(count), "%u CREW", (unsigned)n);
    } else {
      snprintf(count, sizeof(count), "-- CREW");
    }
    const lv_font_t* f = font_for_px(g_.px(kStatusFont));
    lv_point_t size;
    lv_text_get_size(&size, count, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    const int32_t y = g_.cy + g_.px(kStationCountY);
    draw_text(layer, count, f, crew_color::kQuiet, g_.cx - g_.px(0.04f), y);
    draw_bars(layer, g_.cx - g_.px(0.04f) + size.x / 2 + g_.px(0.07f), y,
              g_.pxf(kBarsH), view_.weakest_bars());
    draw_disc(layer);
    draw_text(layer, status.c_str(), f, status_color, g_.cx,
              g_.cy + g_.px(kStationStatusY));
  } else {
    draw_text(layer, view_.channel.c_str(), font_for_px(g_.px(kTitleFont)),
              color::kMarking, g_.cx, g_.cy + g_.px(kTitleY));
    // The weakest link beside the head count, as on the phones.
    draw_bars(layer, g_.cx + g_.pxf(kLcdW / 2 + 0.085f),
              g_.cy + g_.pxf(kLcdY), g_.pxf(0.07f), view_.weakest_bars());
    draw_text(layer, status.c_str(), font_for_px(g_.px(kStatusFont)),
              status_color, g_.cx, g_.cy + g_.px(kStatusY));
  }
  draw_volume(layer);
}

}  // namespace ui
}  // namespace skgauges
