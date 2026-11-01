#pragma once

// What a gauge page accepts from Signal K, and when it stops trusting it --
// the rules behind AGENTS.md's first two non-negotiables, kept free of
// Arduino and SensESP so they are unit-tested on the host (tests/).
//
// SkData calls these on the loop task with millis() and the WebSocket's
// state; nothing here reads a clock or a connection itself.

#include <stdint.h>

#include "gauge_value.h"

namespace skgauges {

// After a (re)connect, signalk-server replays the last value it holds for
// each path -- one per $source, however old -- in a burst, and SensESP drops
// the delta timestamps. Everything a path receives within this window after
// its first sample on a new connection is treated as replay and ignored.
constexpr uint32_t kReplayWindowMs = 1000;
// A null/non-numeric delta blanks the gauge unless another source delivered
// a real value within this time (several sources can publish one path).
constexpr uint32_t kInvalidGraceMs = 2000;

// The Signal K connection a sample arrived on.
struct Connection {
  bool up = false;          // The WebSocket is connected
  uint32_t generation = 0;  // Changes when SensESP builds a new client
  uint32_t epoch = 0;       // Changes on every connection state change
};

// A sample in displayed units (`value2` NAN unless the page has two).
// Returns true if it is now the page's value; false if it was not finite
// (the page is then treated as given an invalid delta), the link is down,
// or it falls in the replay window of a new connection.
bool gate_sample(GaugeValue* slot, float value, float value2, uint32_t now,
                 const Connection& link);

// A null or non-numeric delta: blanks the page at once, or after
// kInvalidGraceMs if a valid value arrived moments ago (another source).
void gate_invalid(GaugeValue* slot, uint32_t now);

// Run about once a second: drops values that have gone stale (so `seen`
// never outlives them, whatever millis() wraps to), ends a grace time no
// valid sample interrupted, and closes a replay window no later sample did.
void gate_sweep(GaugeValue* slot, uint32_t now);

}  // namespace skgauges
