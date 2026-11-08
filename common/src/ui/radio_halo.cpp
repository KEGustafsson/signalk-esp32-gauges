#include "radio_halo.h"

#include <string>

namespace skgauges {
namespace ui {

namespace {

// Proportions (fractions of R). The band covers the bezel, 0.865 to the
// screen's edge; the face and its ticks end at 0.85.
constexpr float kBandOuter = 0.997f;
constexpr float kBandWidth = 0.135f;
constexpr float kAccentOuter = 0.885f;
constexpr float kAccentWidth = 0.016f;
// The glow inside the accent line, down to the face's edge and no further.
constexpr float kGlowWidth = 0.019f;
constexpr float kTextRadius = 0.935f;
constexpr float kTopFont = 0.078f;
constexpr float kBottomFont = 0.058f;
constexpr float kTopMaxDeg = 120.0f;
constexpr float kBottomMaxDeg = 56.0f;
// Lower right, clear of the status dot and the radio pip at 6 o'clock.
constexpr float kBottomAngle = 142.0f;

}  // namespace

RadioHalo::RadioHalo(lv_obj_t* parent, const Geometry& geometry)
    : g_(geometry) {
  obj_ = lv_obj_create(parent);
  lv_obj_remove_style_all(obj_);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(obj_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(obj_, lv_pct(100), lv_pct(100));
  lv_obj_add_event_cb(obj_, draw_cb, LV_EVENT_DRAW_MAIN, this);
}

RadioHalo::~RadioHalo() { lv_obj_delete(obj_); }

void RadioHalo::set(const CrewView& view) {
  const bool was_lit = lit_;
  const bool was_dashed = dashed_;
  const uint32_t was_accent = accent_;
  view_ = view;
  const bool on = view.state == RadioState::kOnChannel;
  std::string top;
  std::string bottom;
  bool relayed = false;
  bool reticulum = false;
  for (const CrewMember& m : view.members) {
    if (!m.talking) continue;
    if (!top.empty()) top += " + ";
    top += m.name;
    relayed = relayed || m.relayed;
    reticulum = reticulum || m.reticulum;
  }
  lit_ = on && (view.transmitting || view.busy || !top.empty());
  dashed_ = false;
  const bool pip_same = drawn_once_ && view.state == pip_state_ &&
                        view.muted == pip_muted_;
  pip_state_ = view.state;
  pip_muted_ = view.muted;
  if (!lit_) {
    const bool same = pip_same && !was_lit;
    top_ = ArcText();
    bottom_ = ArcText();
    top_text_.clear();
    bottom_text_.clear();
    if (!same) invalidate_drawn();
    return;
  }
  if (view.transmitting) {
    accent_ = crew_color::kTransmit;
    top = "TALKING";
    bottom = std::to_string(view.members.size()) + " LISTENING";
  } else if (view.busy) {
    // The key held while someone else has the channel: who, and why
    // nothing goes out.
    accent_ = crew_color::kWarn;
    if (top.empty()) {
      top = "CHANNEL BUSY";
    } else {
      bottom = "CHANNEL BUSY";
    }
  } else {
    accent_ = view.muted ? crew_color::kUnlit : crew_color::kTalk;
    dashed_ = relayed || reticulum;
    if (view.muted) {
      bottom = LV_SYMBOL_MUTE " MUTED";
    } else if (reticulum) {
      bottom = "VIA RETICULUM";
    } else if (relayed) {
      bottom = "VIA RELAY";
    }
  }
  if (pip_same && was_lit && was_dashed == dashed_ && was_accent == accent_ &&
      top == top_text_ && bottom == bottom_text_) {
    return;  // Nothing it draws changed: no new glyphs, no redraw
  }
  const uint32_t top_color =
      view.transmitting ? crew_color::kTransmit : color::kMarking;
  top_ = ArcText(top, font_for_px(g_.px(kTopFont)), top_color,
                 g_.pxf(kTextRadius), kTopMaxDeg);
  bottom_ = bottom.empty()
                ? ArcText()
                : ArcText(bottom, font_for_px(g_.px(kBottomFont)),
                          crew_color::kQuiet, g_.pxf(kTextRadius),
                          kBottomMaxDeg);
  top_text_ = top;
  bottom_text_ = bottom;
  invalidate_drawn();
}

void RadioHalo::invalidate_drawn() {
  drawn_once_ = true;
  // The band, the glow and the text: the ring outside the face.
  const int32_t inner = g_.px(kAccentOuter - kAccentWidth - kGlowWidth) - 1;
  invalidate_ring(obj_, g_.cx, g_.cy, inner, g_.px(kBandOuter) + 1);
  // The pip, beside the status dot (crew_draw.cpp's draw_radio_pip).
  const int32_t px = g_.cx - g_.px(0.075f);
  const int32_t py = g_.cy + g_.px(0.925f);
  const int32_t pr = LV_MAX(4, g_.px(0.016f)) + 3;
  const lv_area_t pip = {px - pr, py - pr, px + pr, py + pr};
  lv_obj_invalidate_area(obj_, &pip);
}

void RadioHalo::draw_cb(lv_event_t* e) {
  const RadioHalo* self = static_cast<const RadioHalo*>(lv_event_get_user_data(e));
  self->draw(lv_event_get_layer(e));
}

void RadioHalo::draw(lv_layer_t* layer) const {
  if (lit_) {
    // A dark band over the chrome so the name reads on any part of it.
    // Opaque: whatever is engraved in the bezel must not show through.
    draw_arc_band(layer, g_, kBandOuter, kBandWidth, crew_color::kBand,
                  LV_OPA_COVER);
    // A soft glow inside the accent line, then the line: solid when heard
    // directly, dashed through a relay or over Reticulum.
    draw_arc_band(layer, g_, kAccentOuter - kAccentWidth, kGlowWidth, accent_,
                  LV_OPA_20);
    if (dashed_) {
      for (int i = 0; i < 48; i++) {
        const float a = i * 7.5f;
        draw_arc_band(layer, g_, kAccentOuter, kAccentWidth, accent_,
                      LV_OPA_COVER, a, a + 4.5f);
      }
    } else {
      draw_arc_band(layer, g_, kAccentOuter, kAccentWidth, accent_,
                    LV_OPA_COVER);
    }
    top_.draw(layer, g_, 0.0f, false);
    bottom_.draw(layer, g_, kBottomAngle, true);
  }
  draw_radio_pip(layer, g_, view_);
}

}  // namespace ui
}  // namespace skgauges
