// SPDX-License-Identifier: EUPL-1.2
#pragma once

// What the radio says it is doing (the Status page's "Radio" line), and the
// rule for it while it runs. Pure and tested (tests/test_radio_state.cpp):
// a radio whose tasks are not all running is Stopped, whatever its clock or
// memory say, and one with neither the WLAN nor Reticulum up has No network,
// so it never reports On channel with nothing behind it.

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
  kNoNetwork,       // Running, clock trusted, but no WLAN socket and no
                    // Reticulum connection: it can neither hear nor be heard
  kStopped,         // A task could not be started, stalled, or would not stop
};

// The state of a radio that got past its start checks: `tasks_running` is
// false once any of its tasks failed to start, stalled for good or did not
// stop when asked; `connected`, whether it has a WLAN socket or a Reticulum
// connection.
inline RadioState running_state(bool tasks_running, Shed shed,
                                ClockSource clock, bool connected) {
  if (!tasks_running) return RadioState::kStopped;
  if (shed == Shed::kPaused) return RadioState::kPaused;
  if (clock == ClockSource::kNone) return RadioState::kNoClock;
  return connected ? RadioState::kOnChannel : RadioState::kNoNetwork;
}

}  // namespace crewradio
