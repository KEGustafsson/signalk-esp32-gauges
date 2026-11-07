#pragma once

// Where a tap on the Crew page lands: the volume arc's - and + ends, and
// its label between them, which switches the arc between the speaker
// volume and the mic gain (ui/crew_page.cpp draws them). Kept free of LVGL
// so it is unit-tested on the host (tests/test_crew_tap.cpp).
//
// The zones are the whole bottom of the dial, below the roster ring's
// lowest names (members reach 118 degrees each side of the top, their bars
// to about 0.49 R down), out to the bezel's rim: wide targets for a wet
// finger on a moving boat. Left of the label lowers, right of it raises.

namespace skgauges {

enum class CrewTap { kNone, kDown, kUp, kSwitch };

constexpr float kCrewTapTop = 0.52f;        // Of R, below the centre
constexpr float kCrewTapLabelHalf = 0.22f;  // The label's zone, each side

// (x, y): the tap from the dial's centre as fractions of its radius, y
// downwards.
inline CrewTap crew_tap(float x, float y) {
  if (!(y >= kCrewTapTop) || x * x + y * y > 1.0f) return CrewTap::kNone;
  if (x < -kCrewTapLabelHalf) return CrewTap::kDown;
  if (x > kCrewTapLabelHalf) return CrewTap::kUp;
  return CrewTap::kSwitch;
}

}  // namespace skgauges
