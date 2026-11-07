// SPDX-License-Identifier: EUPL-1.2
#pragma once

// What the radio says it is doing (the Status page's "Radio" line), and the
// rule for it while it runs. Pure and tested (tests/test_radio_state.cpp):
// a radio whose tasks are not all running is Stopped, whatever its clock or
// memory say, so it never reports On channel with nothing behind it.

#include <stdint.h>

#include "clock_policy.h"
#include "memory_guard.h"

namespace crewradio {

enum class RadioState : uint8_t {
  kOff,             // Disabled in its card
  kNoKey,           // Enabled, but no channel key saved
  kSelfTestFailed,  // The known-answer test failed: never on air
  kNoAudio,         // The codec or I2S did not start
  kNoClock,         // Running, waiting for a clock it can trust
  kOnChannel,
  kPaused,          // The memory guard shed everything
  kStopped,         // A task could not be started, stalled, or would not stop
};

// The state of a radio that got past its start checks: `tasks_running` is
// false once any of its tasks failed to start, stalled for good or did not
// stop when asked.
inline RadioState running_state(bool tasks_running, Shed shed,
                                ClockSource clock) {
  if (!tasks_running) return RadioState::kStopped;
  if (shed == Shed::kPaused) return RadioState::kPaused;
  return clock == ClockSource::kNone ? RadioState::kNoClock
                                     : RadioState::kOnChannel;
}

}  // namespace crewradio
