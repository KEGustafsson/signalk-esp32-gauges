// SPDX-License-Identifier: EUPL-1.2
// Starting, stopping and restarting the radio's tasks, with every failure
// injected through a fake port: no task left running beside a failed one,
// Stopped reported once whichever task failed, and a task that would not
// stop never started again.
#include "core/lifecycle.h"
#include "core/radio_state.h"
#include "test.h"

using namespace crewradio;

namespace {

// Models the tasks: which exist, and whether asking them to stop works.
struct FakePort : TaskPort {
  bool fail_net = false, fail_audio = false, fail_beat = false;
  bool stuck = false;  // A worker that will not leave
  bool net = false, audio = false, beat = false;
  int creates = 0, stop_requests = 0, stopped_calls = 0;

  bool create(RadioTask t) override {
    creates++;
    switch (t) {
      case RadioTask::kNet:
        if (fail_net) return false;
        if (net) duplicate = true;  // Two of the same task: never allowed
        net = true;
        return true;
      case RadioTask::kAudio:
        if (fail_audio) return false;
        if (audio) duplicate = true;
        audio = true;
        return true;
      case RadioTask::kBeat:
        if (fail_beat) return false;
        beat = true;
        return true;
    }
    return false;
  }
  void request_stop() override {
    stop_requests++;
    if (!stuck) net = audio = false;
  }
  bool wait_stopped(int64_t) override { return !net && !audio; }
  void stopped_for_good() override { stopped_calls++; }

  bool duplicate = false;
  bool workers() const { return net || audio; }
};

RadioState reported(const Lifecycle& l) {
  return running_state(!l.stopped(), Shed::kNone, ClockSource::kSntp);
}

}  // namespace

TEST(lifecycle_starts_all_three) {
  FakePort p;
  Lifecycle l(&p);
  CHECK(l.start());
  CHECK(p.net && p.audio && p.beat && !l.stopped());
  CHECK(p.stopped_calls == 0);
  CHECK(reported(l) == RadioState::kOnChannel);
}

TEST(lifecycle_net_fails_at_start_audio_is_stopped_again) {
  FakePort p;
  p.fail_net = true;
  Lifecycle l(&p);
  CHECK(!l.start());
  CHECK(!p.workers() && !p.beat);  // Audio was stopped; beat never made
  CHECK(l.stopped() && p.stopped_calls == 1);
  CHECK(reported(l) == RadioState::kStopped);
}

TEST(lifecycle_audio_fails_at_start_net_is_stopped_again) {
  FakePort p;
  p.fail_audio = true;
  Lifecycle l(&p);
  CHECK(!l.start());
  CHECK(!p.workers() && !p.beat);
  CHECK(p.stopped_calls == 1 && reported(l) == RadioState::kStopped);
}

TEST(lifecycle_beat_fails_at_start_the_workers_are_stopped) {
  FakePort p;
  p.fail_beat = true;
  Lifecycle l(&p);
  CHECK(!l.start());
  CHECK(!p.workers());  // Nothing runs without the supervisor
  CHECK(l.stopped() && p.stopped_calls == 1);
  CHECK(reported(l) == RadioState::kStopped);
}

TEST(lifecycle_a_start_failure_that_cannot_stop_is_counted_and_stopped) {
  FakePort p;
  p.fail_audio = true;
  p.stuck = true;  // Net started and will not leave
  Lifecycle l(&p);
  CHECK(!l.start());
  CHECK(l.did_not_stop() == 1);
  CHECK(l.stopped() && p.stopped_calls == 1);
  CHECK(reported(l) == RadioState::kStopped);
}

TEST(lifecycle_a_stall_restarts_both_workers) {
  FakePort p;
  Lifecycle l(&p);
  l.start();
  const int before = p.creates;
  l.on_stall(Supervisor::Verdict::kRestart);
  CHECK(p.net && p.audio && !p.duplicate);
  CHECK(p.creates == before + 2 && l.restarts() == 1 && !l.stopped());
  CHECK(reported(l) == RadioState::kOnChannel);
}

TEST(lifecycle_a_stall_after_the_last_restart_stops_for_good) {
  FakePort p;
  Lifecycle l(&p);
  l.start();
  l.on_stall(Supervisor::Verdict::kGiveUp);
  CHECK(!p.workers() && l.stopped() && p.stopped_calls == 1);
  CHECK(reported(l) == RadioState::kStopped);
  // Later stalls change nothing (and say nothing again).
  l.on_stall(Supervisor::Verdict::kRestart);
  CHECK(!p.workers() && p.stopped_calls == 1);
}

TEST(lifecycle_a_task_that_will_not_stop_is_never_restarted_beside_itself) {
  FakePort p;
  Lifecycle l(&p);
  l.start();
  p.stuck = true;
  const int before = p.creates;
  l.on_stall(Supervisor::Verdict::kRestart);
  CHECK(p.creates == before);  // No second cr-net or cr-audio
  CHECK(!p.duplicate && l.did_not_stop() == 1);
  CHECK(l.stopped() && p.stopped_calls == 1);
  CHECK(reported(l) == RadioState::kStopped);
}

TEST(lifecycle_a_restart_that_cannot_create_the_workers_stops_for_good) {
  FakePort p;
  Lifecycle l(&p);
  l.start();
  p.fail_audio = true;
  l.on_stall(Supervisor::Verdict::kRestart);
  CHECK(!p.workers());  // The net task it did make is stopped again
  CHECK(l.stopped() && p.stopped_calls == 1 && l.restarts() == 0);
  CHECK(reported(l) == RadioState::kStopped);
}
