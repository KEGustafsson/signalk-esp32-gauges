#pragma once

// Drawing pieces shared by the radio halo and the Crew page (MOCK-UP; see
// crew_view.h).

#include <lvgl.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "crew_view.h"
#include "draw_utils.h"

namespace skgauges {
namespace ui {

namespace crew_color {
constexpr uint32_t kTalk = 0x81C784;      // The app's "talking" green
constexpr uint32_t kTransmit = color::kNeedle;
constexpr uint32_t kWarn = 0xFFB74D;      // Amber: on, but not on air
constexpr uint32_t kQuiet = 0xBDBDBD;     // Status text with nothing to say
constexpr uint32_t kUnlit = 0x5A5A60;     // A bar or a pip that is off
constexpr uint32_t kBand = 0x0A0A0C;      // The halo's band over the bezel
}  // namespace crew_color

// Text set along an arc of the round screen, one glyph image per character,
// each rotated to the arc's tangent: the name of whoever is talking, around
// the bezel. Built once per text, drawn as often as needed.
class ArcText {
 public:
  ArcText() = default;
  // max_deg: the widest arc the text may span; a longer text is cut and
  // ends in "..".
  ArcText(const std::string& text, const lv_font_t* font, uint32_t color,
          float radius_px, float max_deg);
  // Centred on dial_angle_deg (0 = 12 o'clock). Bottom text reads left to
  // right with its feet towards the edge, like the top.
  void draw(lv_layer_t* layer, const Geometry& g, float dial_angle_deg,
            bool bottom, lv_opa_t opa = LV_OPA_COVER) const;
  bool empty() const { return glyphs_.empty(); }
  float span_deg() const { return span_deg_; }

 private:
  struct Glyph {
    std::shared_ptr<TextImage> image;
    float offset_deg;  // From the text's centre, along the arc
  };
  std::vector<Glyph> glyphs_;
  float radius_px_ = 0.0f;
  float span_deg_ = 0.0f;
};

// Four signal bars, the phones' 1-4 levels, centred on (cx, cy) and h tall;
// red at one bar, as on the phones' head-count box. level 0 draws them unlit.
void draw_bars(lv_layer_t* layer, float cx, float cy, float h, uint8_t level,
               lv_opa_t opa = LV_OPA_COVER);

// The Crew page's one status line and its colour.
std::string crew_status_text(const CrewView& view);
uint32_t crew_status_color(const CrewView& view);

// The radio pip beside the status dot: green on channel, amber when on but
// not on air; a red slash while muted. Nothing while the radio is off.
void draw_radio_pip(lv_layer_t* layer, const Geometry& g,
                    const CrewView& view);

void draw_arc_band(lv_layer_t* layer, const Geometry& g, float r_out_frac,
                   float width_frac, uint32_t color, lv_opa_t opa,
                   float from_deg = 0.0f, float to_deg = 360.0f);

}  // namespace ui
}  // namespace skgauges
