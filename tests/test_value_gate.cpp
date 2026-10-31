// What a page accepts from Signal K and when it stops trusting it
// (value_gate.h): AGENTS.md's first two non-negotiables.

#include "test.h"
#include "value_gate.h"

using namespace skgauges;

namespace {

Connection up(uint32_t generation = 1, uint32_t epoch = 1) {
  Connection link;
  link.up = true;
  link.generation = generation;
  link.epoch = epoch;
  return link;
}

// A slot that has passed the replay window of connection `link` at `now`.
GaugeValue live(uint32_t now, const Connection& link) {
  GaugeValue slot;
  gate_sample(&slot, 1, NAN, now - 2000, link);  // Opens the window
  gate_sample(&slot, 2, NAN, now, link);         // Past it: accepted
  return slot;
}

}  // namespace

TEST(replayed_values_after_a_connect_are_ignored) {
  GaugeValue slot;
  const Connection link = up();
  // The first sample on a connection is the server's replay of its last
  // value, however old.
  CHECK(!gate_sample(&slot, 42, NAN, 10000, link));
  CHECK(!slot.seen);
  // Anything within the window is replay too.
  CHECK(!gate_sample(&slot, 43, NAN, 10000 + kReplayWindowMs - 1, link));
  CHECK(!slot.seen);
  CHECK(gate_sample(&slot, 44, NAN, 10000 + kReplayWindowMs, link));
  CHECK(slot.seen);
  CHECK(slot.value == 44);
}

TEST(a_reconnect_opens_a_new_window) {
  GaugeValue slot = live(50000, up(1, 1));
  CHECK(slot.seen);
  // Same client, the WebSocket library reconnected: the epoch moved.
  CHECK(!gate_sample(&slot, 5, NAN, 51000, up(1, 3)));
  CHECK(slot.value == 2);  // Not replaced by the replayed value
  // A new client from SensESP: the generation changed.
  slot = live(50000, up(1, 1));
  CHECK(!gate_sample(&slot, 5, NAN, 51000, up(2, 1)));
}

TEST(nothing_counts_while_the_link_is_down) {
  GaugeValue slot = live(50000, up());
  Connection down = up();
  down.up = false;
  CHECK(!gate_sample(&slot, 99, NAN, 50100, down));
  CHECK(slot.value == 2);
}

TEST(non_finite_values_blank_the_page) {
  GaugeValue slot;
  CHECK(!gate_sample(&slot, INFINITY, NAN, 1000, up()));
  CHECK(!slot.seen);
  slot = live(50000, up());
  // Long after the last good value: blank at once.
  CHECK(!gate_sample(&slot, NAN, NAN, 50000 + kInvalidGraceMs, up()));
  CHECK(!slot.seen);
  slot = live(50000, up());
  CHECK(!gate_sample(&slot, 1, INFINITY, 60000, up()));  // Second value too
  CHECK(!slot.seen);
}

TEST(null_blanks_unless_another_source_confirms) {
  GaugeValue slot = live(50000, up());
  // A null just after a good value: another source may still be sending.
  gate_invalid(&slot, 50100);
  CHECK(slot.seen);
  CHECK(slot.invalid_pending);
  // It does: the page keeps showing data.
  CHECK(gate_sample(&slot, 3, NAN, 50500, up()));
  CHECK(!slot.invalid_pending);
  gate_sweep(&slot, 50100 + kInvalidGraceMs + 10);
  CHECK(slot.seen);
  // Nobody confirms: blank after the grace time.
  CHECK(gate_sample(&slot, 4, NAN, 53000, up()));
  gate_invalid(&slot, 53100);
  gate_sweep(&slot, 53100 + kInvalidGraceMs - 1);
  CHECK(slot.seen);
  gate_sweep(&slot, 53100 + kInvalidGraceMs);
  CHECK(!slot.seen);
  // No recent value at all: blank at once.
  slot = live(50000, up());
  gate_invalid(&slot, 50000 + kInvalidGraceMs);
  CHECK(!slot.seen);
}

TEST(stale_values_show_dashes) {
  GaugeValue slot = live(50000, up());
  slot.stale_ms = 10000;
  CHECK(slot.fresh(59999));
  CHECK(!slot.fresh(60000));
  gate_sweep(&slot, 60000);
  CHECK(!slot.seen);
  // The page's own data timeout.
  slot = live(50000, up());
  slot.stale_ms = 30000;
  gate_sweep(&slot, 75000);
  CHECK(slot.seen);
  gate_sweep(&slot, 80000);
  CHECK(!slot.seen);
}

TEST(millis_wraparound) {
  // A value taken just before millis() wraps stays fresh just after it, and
  // the sweep clears it once stale rather than letting it look new again.
  const uint32_t before_wrap = 0xFFFFFF00u;
  GaugeValue slot = live(before_wrap, up());
  slot.stale_ms = 10000;
  CHECK(slot.fresh(before_wrap + 0x200u));  // Wrapped, 512 ms later
  gate_sweep(&slot, before_wrap + 10000u);
  CHECK(!slot.seen);
  // Unswept, a value ~24.8 days old is not fresh either.
  GaugeValue unswept = live(before_wrap, up());
  CHECK(!unswept.fresh(before_wrap + 0x7FFFFFFFu));
  // A replay window spanning the wrap still closes.
  GaugeValue fresh_slot;
  CHECK(!gate_sample(&fresh_slot, 1, NAN, 0xFFFFFE00u, up()));
  CHECK(!gate_sample(&fresh_slot, 1, NAN, 0x100u, up()));  // Inside
  CHECK(gate_sample(&fresh_slot, 1, NAN, 0x400u, up()));   // After 1.5 s
}

TEST(quiet_path_closes_its_replay_window) {
  GaugeValue slot;
  gate_sample(&slot, 1, NAN, 1000, up());
  CHECK(slot.replaying);
  gate_sweep(&slot, 1000 + kReplayWindowMs);
  CHECK(!slot.replaying);
}
