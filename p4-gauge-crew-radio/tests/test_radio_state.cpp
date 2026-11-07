// SPDX-License-Identifier: EUPL-1.2
// What the radio reports while it runs: Stopped wins over everything.
#include "core/radio_state.h"
#include "test.h"

using namespace crewradio;

TEST(radio_state_stopped_whenever_a_task_is_not_running) {
  for (Shed shed : {Shed::kNone, Shed::kGroupCopies, Shed::kPaused}) {
    for (ClockSource c :
         {ClockSource::kNone, ClockSource::kSntp, ClockSource::kGnss}) {
      CHECK(running_state(false, shed, c, true) == RadioState::kStopped);
    }
  }
}

TEST(radio_state_paused_then_clock_while_running) {
  CHECK(running_state(true, Shed::kPaused, ClockSource::kSntp, true) ==
        RadioState::kPaused);
  CHECK(running_state(true, Shed::kTransmit, ClockSource::kNone, true) ==
        RadioState::kNoClock);
  CHECK(running_state(true, Shed::kNone, ClockSource::kGnss, true) ==
        RadioState::kOnChannel);
}

// Running with a clock, but neither transport up: it can neither hear nor be
// heard, so it does not say On channel.
TEST(radio_state_without_a_transport_is_no_network) {
  CHECK(running_state(true, Shed::kNone, ClockSource::kSntp, false) ==
        RadioState::kNoNetwork);
  CHECK(running_state(true, Shed::kNone, ClockSource::kNone, false) ==
        RadioState::kNoClock);  // The clock first: it is what it waits for
  CHECK(running_state(false, Shed::kNone, ClockSource::kSntp, false) ==
        RadioState::kStopped);
}
