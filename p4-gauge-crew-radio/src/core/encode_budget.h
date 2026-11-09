// SPDX-License-Identifier: EUPL-1.2
#pragma once

// How hard the Opus encoder may work, from how long it takes (2026-11-08).
// On the 4" the encoder's state is in PSRAM, behind a 128 KB cache the
// display's drawing on the other core shares: with the screen busy (a crew
// member talking lights the halo) an encode at complexity 5 took 18-27 ms of
// a 20 ms frame, cr-audio took 96 % of core 0, and the task watchdog
// restarted the unit after 5 s without idle. So the encode is timed every
// frame: kWindow frames in which the slowest took more than kHighUs (half a
// frame, leaving room for the mic's cleanup and the rest of the pass) step
// the complexity down one, to 0 at the least. It never steps back up while
// the radio runs: the conditions that slowed it come back, and a lower
// complexity is the same Opus to every receiver, a little less clean. Pure.

#include <stdint.h>

namespace crewradio {

class EncodeBudget {
 public:
  static constexpr int kWindow = 10;       // 200 ms of frames
  static constexpr int kHighUs = 10000;    // Half a 20 ms frame

  explicit EncodeBudget(int complexity) : complexity_(complexity) {}

  // One encode's time. True when the complexity changed (complexity()).
  bool took(int us);
  int complexity() const { return complexity_; }

 private:
  int complexity_;
  int frames_ = 0;
  int slowest_ = 0;
};

}  // namespace crewradio
