// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The radio's own watchdog: each radio task checks in every loop; one that
// has not checked in for kStallMs is stalled, and the supervisor tears the
// radio down and starts it again -- never the unit (the gauge's display and
// its restart rules are not the radio's to touch). After kMaxRestarts within
// kWindowMs it gives up and leaves the radio off, with a status. Pure: the
// caller supplies the clock and does the restarting. The check-in is an
// atomic store of the low 32 bits of the clock (lock-free on the P4's 32-bit
// RISC-V; 64-bit atomics are not), compared wrap-safely, so tasks may check
// in while the supervisor reads.

#include <stdint.h>

#include <atomic>

namespace crewradio {

class Supervisor {
 public:
  static constexpr int kSlots = 4;
  static constexpr int64_t kStallMs = 2000;
  static constexpr int kMaxRestarts = 3;
  static constexpr int64_t kWindowMs = 10LL * 60 * 1000;

  enum class Verdict : uint8_t { kOk, kRestart, kGiveUp };

  void arm(int slot, int64_t now_ms) {
    seen_[slot].store((uint32_t)now_ms, std::memory_order_relaxed);
    armed_[slot].store(true, std::memory_order_release);
  }
  void disarm(int slot) { armed_[slot].store(false, std::memory_order_release); }
  void checkin(int slot, int64_t now_ms) {
    seen_[slot].store((uint32_t)now_ms, std::memory_order_relaxed);
  }

  // The slot that stalled, or -1.
  int stalled(int64_t now_ms) const {
    for (int i = 0; i < kSlots; i++) {
      // Armed first: its acquire orders the timestamp read after it, so a
      // slot just re-armed is never judged by its previous task's time.
      if (!armed_[i].load(std::memory_order_acquire)) continue;
      const uint32_t since =
          (uint32_t)now_ms - seen_[i].load(std::memory_order_relaxed);
      if ((int32_t)since > (int32_t)kStallMs) {
        return i;
      }
    }
    return -1;
  }

  // What to do about a stall at now_ms: restart, or give up after too many.
  Verdict on_stall(int64_t now_ms) {
    if (restarts_ > 0 && now_ms - first_restart_ms_ > kWindowMs) restarts_ = 0;
    if (restarts_ == 0) first_restart_ms_ = now_ms;
    if (restarts_ >= kMaxRestarts) return Verdict::kGiveUp;
    restarts_++;
    return Verdict::kRestart;
  }
  int restarts() const { return restarts_; }

 private:
  std::atomic<uint32_t> seen_[kSlots] = {};
  std::atomic<bool> armed_[kSlots] = {};
  int restarts_ = 0;
  int64_t first_restart_ms_ = 0;
};

}  // namespace crewradio
