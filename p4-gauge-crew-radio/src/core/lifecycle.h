// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Starting, stopping and restarting the radio's tasks: the decisions,
// without FreeRTOS, so every failure can be injected and tested
// (tests/test_lifecycle.cpp). The node's port (esp/radio_runtime.cpp)
// creates the tasks with xTaskCreatePinnedToCore, asks them to stop (a
// generation bump they see at their next turn) and waits for them.
//
// The rules:
//  - The two workers (cr-net, cr-audio) start together or not at all: one
//    that started beside one that could not is stopped again.
//  - cr-beat (the supervisor and the clock) starts after them; without it
//    the workers are stopped too.
//  - A stalled worker gets both stopped and started again; after the
//    supervisor's last restart, or when one does not stop within the wait,
//    or when a restart cannot create them, the radio stays stopped for
//    good. A task that did not stop is never started again beside itself.
//  - Every way of stopping for good calls TaskPort::stopped_for_good()
//    once, which reports Stopped at once: the status never shows On channel
//    with nothing running, whichever task failed.

#include <stdint.h>

#include <atomic>

#include "supervisor.h"

namespace crewradio {

enum class RadioTask : uint8_t { kNet, kAudio, kBeat };

class TaskPort {
 public:
  virtual ~TaskPort() = default;
  // Creates the task; false if it could not be.
  virtual bool create(RadioTask task) = 0;
  // Asks the workers to leave at their next turn.
  virtual void request_stop() = 0;
  // Waits up to `timeout_ms` for both workers to have left; true if they did.
  virtual bool wait_stopped(int64_t timeout_ms) = 0;
  // The radio is off for good: stop watching the tasks, report Stopped.
  virtual void stopped_for_good() = 0;
};

class Lifecycle {
 public:
  static constexpr int64_t kStopWaitMs = 2000;

  explicit Lifecycle(TaskPort* port) : port_(port) {}

  // The workers, then cr-beat. False: stopped for good (said so).
  bool start() {
    if (!start_workers()) return false;
    if (!port_->create(RadioTask::kBeat)) {
      give_up(/*stop_workers=*/true);
      return false;
    }
    return true;
  }

  // A worker stalled and the supervisor said what to do about it.
  void on_stall(Supervisor::Verdict verdict) {
    if (stopped_) return;
    port_->request_stop();
    const bool exited = port_->wait_stopped(kStopWaitMs);
    if (!exited) did_not_stop_++;
    if (verdict == Supervisor::Verdict::kRestart && exited &&
        start_workers()) {
      restarts_++;
      return;
    }
    if (!stopped_) give_up(/*stop_workers=*/false);  // Asked to stop above
  }

  bool stopped() const { return stopped_.load(); }
  int restarts() const { return restarts_; }
  // Times a worker did not leave within kStopWaitMs (logged by the port).
  int did_not_stop() const { return did_not_stop_; }

 private:
  bool start_workers() {
    const bool net = port_->create(RadioTask::kNet);
    const bool audio = port_->create(RadioTask::kAudio);
    if (net && audio) return true;
    give_up(/*stop_workers=*/true);
    return false;
  }

  void give_up(bool stop_workers) {
    if (stop_workers) {
      port_->request_stop();
      if (!port_->wait_stopped(kStopWaitMs)) did_not_stop_++;
    }
    stopped_.store(true);
    port_->stopped_for_good();
  }

  TaskPort* port_;
  std::atomic<bool> stopped_{false};
  int restarts_ = 0;
  int did_not_stop_ = 0;
};

}  // namespace crewradio
