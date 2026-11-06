#include "arc_fit.h"

#include <vector>

#include "test.h"

using skgauges::arc_fit;
using skgauges::ArcFit;

namespace {

// A radius at which one px of text spans one degree: the cases read in
// degrees.
constexpr float kOneDegree = 57.29577951308232f;

ArcFit fit(const std::vector<float>& w, std::vector<bool> spaces,
           float max_deg, float dot = 4.0f) {
  if (spaces.empty()) spaces.assign(w.size(), false);
  bool s[64];
  for (size_t i = 0; i < spaces.size(); i++) s[i] = spaces[i];
  return arc_fit(w.data(), s, w.size(), dot, kOneDegree, max_deg);
}

}  // namespace

TEST(arc_fit_keeps_a_text_that_fits_whole) {
  // Nine 10 px glyphs and a last one of 10: exactly 100 degrees.
  std::vector<float> w(9, 10.0f);
  w.push_back(10.0f);
  const ArcFit f = fit(w, {}, 100.0f);
  CHECK(f.kept == 10 && !f.cut);
  CHECK_NEAR(f.width_px, 100.0, 1e-3);
}

TEST(arc_fit_cuts_when_only_the_last_glyph_overflows) {
  // The boundary the review asked for: the first nine (90) plus ".." (8)
  // fit in 100, but the nine plus the last glyph (15) do not.
  std::vector<float> w(9, 10.0f);
  w.push_back(15.0f);
  const ArcFit f = fit(w, {}, 100.0f);
  CHECK(f.cut);
  CHECK(f.kept == 9);
  CHECK_NEAR(f.width_px, 98.0, 1e-3);
  CHECK(f.width_px <= 100.0f);
}

TEST(arc_fit_never_runs_past_the_arc) {
  // Every length and a last glyph of every width: the result always fits,
  // and a text that fits is never cut.
  for (int n = 0; n < 30; n++) {
    for (float last = 1.0f; last < 40.0f; last += 3.0f) {
      std::vector<float> w((size_t)n, 7.0f);
      w.push_back(last);
      const ArcFit f = fit(w, {}, 120.0f);
      CHECK(f.width_px <= 120.0f + 1e-3f);
      const float total = 7.0f * (float)n + last;
      CHECK(f.cut == (total > 120.0f));
      if (!f.cut) CHECK(f.kept == w.size());
    }
  }
}

TEST(arc_fit_leaves_no_space_before_the_dots) {
  // "AB CD..." where the cut would fall just after the space.
  const std::vector<float> w = {10, 10, 6, 10, 10, 10, 10};
  const std::vector<bool> s = {false, false, true, false, false, false, false};
  const ArcFit f = fit(w, s, 34.0f);  // 10 + 10 + 6 + 8 dots = 34
  CHECK(f.cut && f.kept == 2);
  CHECK_NEAR(f.width_px, 28.0, 1e-3);
}

TEST(arc_fit_an_arc_too_short_for_anything_keeps_only_the_dots) {
  const ArcFit f = fit({20, 20}, {}, 10.0f);
  CHECK(f.cut && f.kept == 0);
  CHECK_NEAR(f.width_px, 8.0, 1e-3);
  const ArcFit empty = fit({}, {}, 10.0f);
  CHECK(empty.kept == 0 && !empty.cut);
}
