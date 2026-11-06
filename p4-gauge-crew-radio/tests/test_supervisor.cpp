// SPDX-License-Identifier: EUPL-1.2
// The radio's watchdog and the memory ladder.
#include "core/memory_guard.h"
#include "core/supervisor.h"
#include "test.h"

using namespace crewradio;

TEST(supervisor_a_task_that_stops_checking_in_is_stalled) {
  Supervisor s;
  CHECK(s.stalled(100000) == -1);  // Nothing armed
  s.arm(0, 0);
  s.arm(1, 0);
  s.checkin(0, 1500);
  CHECK(s.stalled(2000) == -1);
  CHECK(s.stalled(2001) == 1);
  s.disarm(1);
  CHECK(s.stalled(3000) == -1);
  CHECK(s.stalled(3501) == 0);
}

TEST(supervisor_survives_the_32_bit_wrap) {
  Supervisor s;
  const int64_t t = 0xFFFFFF00LL;
  s.arm(0, t);
  CHECK(s.stalled(t + 1000) == -1);  // Wrapped in 32 bits, 1 s later
  CHECK(s.stalled(t + 2001) == 0);
}

TEST(supervisor_gives_up_after_three_restarts_in_ten_minutes) {
  Supervisor s;
  CHECK(s.on_stall(0) == Supervisor::Verdict::kRestart);
  CHECK(s.on_stall(1000) == Supervisor::Verdict::kRestart);
  CHECK(s.on_stall(2000) == Supervisor::Verdict::kRestart);
  CHECK(s.on_stall(3000) == Supervisor::Verdict::kGiveUp);
  // Ten minutes after the first, the count starts over.
  CHECK(s.on_stall(Supervisor::kWindowMs + 1) == Supervisor::Verdict::kRestart);
}

TEST(memory_guard_steps_down_one_floor_at_a_time_with_hysteresis) {
  MemoryGuard g;
  CHECK(g.update(300 * 1024) == Shed::kNone);
  CHECK(g.update(10 * 1024) == Shed::kReticulum);  // One step per update
  CHECK(g.update(10 * 1024) == Shed::kGroupCopies);
  CHECK(g.update(10 * 1024) == Shed::kTransmit);
  CHECK(g.update(10 * 1024) == Shed::kPaused);
  CHECK(g.update(10 * 1024) == Shed::kPaused);
  // Back up only with 16 KB above the floor that was crossed (32 KB).
  CHECK(g.update(32 * 1024 + MemoryGuard::kHysteresis - 1) == Shed::kPaused);
  CHECK(g.update(32 * 1024 + MemoryGuard::kHysteresis) == Shed::kTransmit);
  CHECK(g.update(300 * 1024) == Shed::kGroupCopies);
  CHECK(g.update(300 * 1024) == Shed::kReticulum);
  CHECK(g.update(300 * 1024) == Shed::kNone);
}
