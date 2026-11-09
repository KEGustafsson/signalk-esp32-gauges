#include "crew_draw.h"

#include "../arc_fit.h"
#include "gauge_page.h"  // in_clip()

#include <math.h>
#include <string.h>

namespace skgauges {
namespace ui {

namespace {
}  // namespace


namespace {

constexpr float kRadToDeg = 57.29577951308232f;

// The code point at text[*i] (UTF-8), advancing *i past it.
uint32_t next_cp(const std::string& text, size_t* i) {
  const uint8_t c = (uint8_t)text[*i];
  int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  uint32_t cp = extra == 0 ? c : (c & (0x3F >> extra));
  (*i)++;
  for (; extra > 0 && *i < text.size(); extra--, (*i)++) {
    cp = (cp << 6) | ((uint8_t)text[*i] & 0x3F);
  }
  return cp;
}

}  // namespace

ArcText::ArcText(const std::string& text, const lv_font_t* font,
                 uint32_t color, float radius_px, float max_deg)
    : radius_px_(radius_px) {
  if (radius_px <= 0.0f) return;
  // Engraved look: a little tracking between the capitals.
  const float tracking = lv_font_get_line_height(font) * 0.08f;
  struct Piece {
    std::string utf8;
    float width;
  };
  std::vector<Piece> pieces;
  for (size_t i = 0; i < text.size();) {
    const size_t start = i;
    const uint32_t cp = next_cp(text, &i);
    pieces.push_back({text.substr(start, i - start),
                      lv_font_get_glyph_width(font, cp, 0) + tracking});
  }
  // The rule is arc_fit.h's (unit-tested): whole if it fits, else cut with
  // "..".
  std::vector<float> widths(pieces.size());
  std::unique_ptr<bool[]> spaces(new bool[pieces.size() + 1]);
  for (size_t i = 0; i < pieces.size(); i++) {
    widths[i] = pieces[i].width;
    spaces[i] = pieces[i].utf8 == " ";
  }
  const float dot_w = lv_font_get_glyph_width(font, '.', '.') + tracking;
  const ArcFit fit = arc_fit(widths.data(), spaces.get(), pieces.size(),
                             dot_w, radius_px, max_deg);
  pieces.resize(fit.kept);
  if (fit.cut) {
    pieces.push_back({".", dot_w});
    pieces.push_back({".", dot_w});
  }
  const float total = fit.width_px;
  span_deg_ = total / radius_px * kRadToDeg;
  float at = -total / 2.0f;
  for (const Piece& p : pieces) {
    const float centre = at + p.width / 2.0f;
    at += p.width;
    if (p.utf8 == " ") continue;
    Glyph g;
    g.image = std::make_shared<TextImage>(p.utf8.c_str(), font, color, false,
                                          glyph_handlers());
    g.offset_deg = centre / radius_px * kRadToDeg;
    glyphs_.push_back(g);
  }
}

void set_arc_text_handlers(const lv_draw_buf_handlers_t* handlers) {
  set_glyph_handlers(handlers);  // The LCDs' characters too (draw_utils.h)
}

void ArcText::draw(lv_layer_t* layer, const Geometry& g, float dial_angle_deg,
                   bool bottom, lv_opa_t opa) const {
  for (const Glyph& glyph : glyphs_) {
    // Top: left to right is clockwise. Bottom: left to right is
    // anticlockwise, and each glyph turns half a circle less.
    const float a = bottom ? dial_angle_deg - glyph.offset_deg
                           : dial_angle_deg + glyph.offset_deg;
    const lv_point_precise_t p = polar(g, a, radius_px_);
    const float rotation = bottom ? a - 180.0f : a;
    if (!in_clip(layer, p.x, p.y, p.x, p.y, glyph.image->radius())) continue;
    glyph.image->draw(layer, p.x, p.y, rotation, opa);
  }
}

void draw_bars(lv_layer_t* layer, float cx, float cy, float h, uint8_t level,
               lv_opa_t opa) {
  const float bar_w = h * 0.22f;
  const float gap = h * 0.12f;
  const float total = 4 * bar_w + 3 * gap;
  const uint32_t lit = level == 1 ? color::kNeedle : color::kMarking;
  for (int i = 0; i < 4; i++) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(i < level ? lit : crew_color::kUnlit);
    dsc.bg_opa = opa;
    dsc.radius = 1;
    const float bh = h * (0.3f + 0.7f * i / 3.0f);
    const int32_t x1 = (int32_t)lroundf(cx - total / 2 + i * (bar_w + gap));
    const int32_t y2 = (int32_t)lroundf(cy + h / 2);
    lv_area_t area = {x1, (int32_t)lroundf(y2 - bh),
                      x1 + LV_MAX(1, (int32_t)lroundf(bar_w)) - 1, y2};
    lv_draw_rect(layer, &dsc, &area);
  }
}

// The Crew page's status line: the radio's state, or who is talking,
// transmitting, busy or muted.
std::string crew_status_text(const CrewView& view) {
  switch (view.state) {
    case RadioState::kOff:
      return "RADIO OFF";
    case RadioState::kStarting:
      return "STARTING...";
    case RadioState::kNoKey:
      return "NO CHANNEL KEY";
    case RadioState::kNoAudio:
      return "NO AUDIO";
    case RadioState::kStopped:
      return "RADIO STOPPED";
    case RadioState::kNoClock:
      return "NO CLOCK";
    case RadioState::kNoNetwork:
      return "NO NETWORK";
    case RadioState::kPausedMemory:
      return "PAUSED: MEMORY";
    case RadioState::kSelfTestFailed:
      return "SELF-TEST FAILED";
    case RadioState::kOnChannel:
      break;
  }
  if (view.transmitting) return "TRANSMITTING";
  std::string talkers;
  for (const CrewMember& m : view.members) {
    if (!m.talking) continue;
    if (!talkers.empty()) talkers += ", ";
    talkers += m.name;
  }
  // Held, but someone else has the channel.
  if (view.busy) return talkers.empty() ? "CHANNEL BUSY" : "BUSY: " + talkers;
  if (!talkers.empty()) return talkers + " TALKING";
  return view.muted ? "ON CHANNEL  " LV_SYMBOL_MUTE : "ON CHANNEL";
}

// The status line's colour for the same states (amber for busy and warnings).
uint32_t crew_status_color(const CrewView& view) {
  switch (view.state) {
    case RadioState::kOnChannel:
      if (view.transmitting) return crew_color::kTransmit;
      if (view.busy) return crew_color::kWarn;
      return view.anyone_talking() ? crew_color::kTalk : crew_color::kQuiet;
    case RadioState::kSelfTestFailed:
    case RadioState::kNoAudio:
    case RadioState::kStopped:
      return color::kNeedle;
    case RadioState::kOff:
    case RadioState::kStarting:
      return crew_color::kQuiet;
    default:
      return crew_color::kWarn;
  }
}

void draw_radio_pip(lv_layer_t* layer, const Geometry& g,
                    const CrewView& view) {
  if (view.state == RadioState::kOff) return;
  // Left of the status dot (gauge_ui.cpp: centred, 0.925 R down).
  const int32_t cx = g.cx - g.px(0.075f);
  const int32_t cy = g.cy + g.px(0.925f);
  const int32_t r = LV_MAX(4, g.px(0.016f));
  if (!in_clip(layer, cx, cy, cx, cy, (float)r + 2.0f)) return;
  uint32_t c = crew_color::kTalk;
  if (view.state == RadioState::kSelfTestFailed ||
      view.state == RadioState::kNoAudio ||
      view.state == RadioState::kStopped) {
    c = color::kNeedle;
  } else if (view.state != RadioState::kOnChannel) {
    c = crew_color::kWarn;
  }
  draw_circle(layer, cx, cy, r + 1, 0x101010);
  draw_circle(layer, cx, cy, r, c);
  if (view.muted) {
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = lv_color_hex(color::kNeedle);
    line.width = LV_MAX(2, r / 2);
    line.round_start = line.round_end = 1;
    line.p1 = {(float)(cx - r * 1.4f), (float)(cy - r * 1.4f)};
    line.p2 = {(float)(cx + r * 1.4f), (float)(cy + r * 1.4f)};
    lv_draw_line(layer, &line);
  }
}

namespace {
CrewViewSource g_crew_source = nullptr;
CrewAdjust g_crew_adjust = nullptr;
}  // namespace

void set_crew_view_source(CrewViewSource source) { g_crew_source = source; }

void set_crew_adjust(CrewAdjust adjust) { g_crew_adjust = adjust; }

bool crew_adjust(CrewLevel level, int dir) {
  return g_crew_adjust != nullptr && g_crew_adjust(level, dir);
}

bool crew_view_available() { return g_crew_source != nullptr; }

CrewView read_crew_view() {
  CrewView v;
  if (g_crew_source == nullptr || !g_crew_source(&v)) {
    v = CrewView();
    v.state = RadioState::kOff;
  }
  return v;
}

bool ring_in_clip(const lv_layer_t* layer, int32_t cx, int32_t cy,
                  int32_t r_out, int32_t r_in) {
  // LVGL's arc covers its circle's whole bounding square and masks every row
  // of the clip it meets, so a needle's small refresh area in the middle of
  // the face would pay for the whole ring, once per dash. Skipped when the
  // clip lies wholly outside the ring's outer circle or wholly inside its
  // hole: the nearest and the farthest point of the clip from the centre.
  const lv_area_t& c = layer->_clip_area;
  const int64_t near_x = c.x1 > cx ? c.x1 - cx : cx > c.x2 ? cx - c.x2 : 0;
  const int64_t near_y = c.y1 > cy ? c.y1 - cy : cy > c.y2 ? cy - c.y2 : 0;
  const int64_t far_x = LV_MAX(LV_ABS(c.x1 - cx), LV_ABS(c.x2 - cx));
  const int64_t far_y = LV_MAX(LV_ABS(c.y1 - cy), LV_ABS(c.y2 - cy));
  const int64_t near2 = near_x * near_x + near_y * near_y;
  const int64_t far2 = far_x * far_x + far_y * far_y;
  const int64_t ro = r_out + 1, ri = r_in - 1;
  if (near2 > ro * ro) return false;
  if (ri > 0 && far2 < ri * ri) return false;
  return true;
}

void draw_arc_band(lv_layer_t* layer, const Geometry& g, float r_out_frac,
                   float width_frac, uint32_t color, lv_opa_t opa,
                   float from_deg, float to_deg) {
  const int32_t radius = g.px(r_out_frac);
  const int32_t width = LV_MAX(1, g.px(width_frac));
  if (!ring_in_clip(layer, g.cx, g.cy, radius, radius - width)) return;
  lv_draw_arc_dsc_t arc;
  lv_draw_arc_dsc_init(&arc);
  arc.center.x = g.cx;
  arc.center.y = g.cy;
  arc.radius = (uint16_t)radius;
  arc.width = width;
  arc.color = lv_color_hex(color);
  arc.opa = opa;
  if (to_deg - from_deg >= 360.0f) {
    arc.start_angle = 0;
    arc.end_angle = 360;
  } else {
    arc.start_angle = to_lv_arc_angle(from_deg);
    arc.end_angle = to_lv_arc_angle(to_deg);
    // A part of the ring (a dash): only where its own bounds meet the clip.
    lv_area_t area;
    lv_draw_arc_get_area(g.cx, g.cy, arc.radius, arc.start_angle,
                         arc.end_angle, arc.width, arc.rounded, &area);
    const lv_area_t& c = layer->_clip_area;
    if (area.x2 < c.x1 || area.x1 > c.x2 || area.y2 < c.y1 || area.y1 > c.y2) {
      return;
    }
  }
  lv_draw_arc(layer, &arc);
}

}  // namespace ui
}  // namespace skgauges
