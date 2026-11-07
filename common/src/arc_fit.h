#pragma once

// How much of a text fits along an arc: the rule ui/crew_draw.cpp's ArcText
// (a talker's name around the bezel) cuts names by, kept free of LVGL so it
// is unit-tested on the host (tests/test_arc_fit.cpp).
//
// The whole text, the last glyph included, is measured against the arc
// first: a text that fits is kept whole. One that does not keeps as many
// glyphs as fit with two dots after them, and no space before the dots.

#include <stddef.h>

namespace skgauges {

struct ArcFit {
  size_t kept;     // Glyphs kept from the start of the text
  bool cut;        // ".." follows them
  float width_px;  // The kept glyphs, and the dots when cut
};

// `widths` are the glyphs' advance widths in px, `spaces` says which are
// spaces; `dot_px` is one dot's width. The arc's radius is `radius_px` and
// the text may span at most `max_deg` of it.
inline ArcFit arc_fit(const float* widths, const bool* spaces, size_t n,
                      float dot_px, float radius_px, float max_deg) {
  const float kRadToDeg = 57.29577951308232f;
  // A thousandth of a degree of slack: px -> degrees rounds, and a text
  // exactly as long as its arc must not be cut by it.
  const float limit = max_deg + 1e-3f;
  auto deg = [radius_px, kRadToDeg](float px) {
    return px / radius_px * kRadToDeg;
  };
  float total = 0.0f;
  for (size_t i = 0; i < n; i++) total += widths[i];
  if (radius_px <= 0.0f || deg(total) <= limit) return {n, false, total};
  float kept = 2.0f * dot_px;
  size_t k = 0;
  while (k < n && deg(kept + widths[k]) <= limit) {
    kept += widths[k];
    k++;
  }
  while (k > 0 && spaces[k - 1]) {  // No space before ".."
    kept -= widths[k - 1];
    k--;
  }
  return {k, true, kept};
}

}  // namespace skgauges
