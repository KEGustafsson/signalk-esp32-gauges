// SPDX-License-Identifier: EUPL-1.2
#pragma once

// One ingress budget per WLAN source address, charged before a datagram is
// parsed at all, as the app's transport/SourceLimiter.kt (and the plugin's
// PeerBudget): 500/s, burst 1000, at most 64 sources, the least recently
// used one evicted, never a newcomer refused.
//
// RateLimiter's global bucket is charged before the AEAD and so cannot tell
// who sent what: first come, first served, a laptop on the boat WLAN that
// simply sends faster than the crew takes the whole of it, and the crew's
// own frames are refused behind it, with no channel key needed. This bucket
// makes a flood cost the flooder: one host spends only its own share. Sized
// well above anything real (a relay carrying several talkers stays well
// inside it). A flood that rotates its address only evicts other strangers'
// buckets; every crew member's comes back full when it is next heard.
//
// Tokens in thousandths, integers, as RateLimiter. Not thread-safe: the
// network task owns it.

#include <stdint.h>

namespace crewradio {

class SourceLimiter {
 public:
  struct Config {
    int64_t per_second = 500;
    int64_t burst = 1000;
  };
  static constexpr int kMaxSources = 64;

  SourceLimiter() : SourceLimiter(Config()) {}
  explicit SourceLimiter(const Config& config) : c_(config) {}

  // True if a datagram from `source` (IPv4, host order) at now_ms is within
  // that source's own budget.
  bool allow(uint32_t source, int64_t now_ms);

  // Forgets every source (a new network).
  void clear();

  int size() const;

 private:
  struct Entry {
    uint32_t source = 0;
    bool used = false;
    int64_t milli = 0;    // Tokens x 1000
    int64_t last_ms = 0;  // Last refill
    uint64_t stamp = 0;   // Last use, for the LRU
  };

  Config c_;
  Entry e_[kMaxSources];
  uint64_t clock_ = 0;
};

}  // namespace crewradio
