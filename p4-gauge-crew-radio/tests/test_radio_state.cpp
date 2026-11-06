// SPDX-License-Identifier: EUPL-1.2
// What the radio reports while it runs: Stopped wins over everything.
#include "core/radio_state.h"
#include "test.h"

using namespace crewradio;

TEST(radio_state_stopped_whenever_a_task_is_not_running) {
  for (Shed shed : {Shed::kNone, Shed::kGroupCopies, Shed::kPaused}) {
    for (ClockSource c :
         {ClockSource::kNone, ClockSource::kSntp, ClockSource::kGnss}) {
      CHECK(running_state(false, shed, c) == RadioState::kStopped);
    }
  }
}

TEST(radio_state_paused_then_clock_while_running) {
  CHECK(running_state(true, Shed::kPaused, ClockSource::kSntp) ==
        RadioState::kPaused);
  CHECK(running_state(true, Shed::kTransmit, ClockSource::kNone) ==
        RadioState::kNoClock);
  CHECK(running_state(true, Shed::kNone, ClockSource::kGnss) ==
        RadioState::kOnChannel);
}
