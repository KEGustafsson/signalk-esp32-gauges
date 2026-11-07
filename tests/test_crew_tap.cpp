#include "crew_tap.h"

#include <math.h>

#include "test.h"

using skgauges::crew_tap;
using skgauges::CrewTap;

namespace {

// A point at dial angle `deg` (clockwise from 12 o'clock), `r` of R out.
CrewTap at(float deg, float r) {
  const float a = deg * 3.14159265f / 180.0f;
  return crew_tap(r * sinf(a), -r * cosf(a));
}

}  // namespace

TEST(crew_tap_finds_the_volume_arcs_minus_plus_and_label) {
  // ui/crew_page.cpp: the arc at 0.79 R from 148 to 212 degrees, "-" at
  // 219, "+" at 141, the label at 0.635 R straight down.
  CHECK(at(219.0f, 0.79f) == CrewTap::kDown);
  CHECK(at(141.0f, 0.79f) == CrewTap::kUp);
  CHECK(crew_tap(0.0f, 0.635f) == CrewTap::kSwitch);
  // The arc's own ends are its - and + too.
  CHECK(at(212.0f, 0.79f) == CrewTap::kDown);
  CHECK(at(148.0f, 0.79f) == CrewTap::kUp);
}

TEST(crew_tap_leaves_the_roster_and_the_middle_alone) {
  CHECK(crew_tap(0.0f, 0.0f) == CrewTap::kNone);     // The LCD
  CHECK(crew_tap(0.0f, 0.20f) == CrewTap::kNone);    // The status line
  CHECK(crew_tap(0.0f, -0.70f) == CrewTap::kNone);   // A member at the top
  // The lowest members (118 degrees each side) and their names and bars.
  CHECK(at(118.0f, 0.70f) == CrewTap::kNone);
  CHECK(at(242.0f, 0.70f) == CrewTap::kNone);
  CHECK(crew_tap(0.62f, 0.33f + 0.085f + 0.055f + 0.03f) == CrewTap::kNone);
  CHECK(crew_tap(-0.62f, 0.50f) == CrewTap::kNone);
}

TEST(crew_tap_ends_at_the_rim) {
  CHECK(crew_tap(0.0f, 0.99f) == CrewTap::kSwitch);
  CHECK(crew_tap(0.0f, 1.01f) == CrewTap::kNone);
  // A square screen's corner, outside the round dial.
  CHECK(crew_tap(0.9f, 0.9f) == CrewTap::kNone);
  CHECK(crew_tap(-0.9f, 0.9f) == CrewTap::kNone);
  CHECK(crew_tap(NAN, NAN) == CrewTap::kNone);
}

TEST(crew_tap_splits_the_bottom_at_the_label) {
  CHECK(crew_tap(-0.23f, 0.7f) == CrewTap::kDown);
  CHECK(crew_tap(-0.21f, 0.7f) == CrewTap::kSwitch);
  CHECK(crew_tap(0.21f, 0.7f) == CrewTap::kSwitch);
  CHECK(crew_tap(0.23f, 0.7f) == CrewTap::kUp);
}
