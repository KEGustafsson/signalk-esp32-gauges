// SPDX-License-Identifier: EUPL-1.2
#pragma once

// rnsd's announce rate rule for one destination (Transport.inbound, rnsd
// 1.5.4), as the app's AnnounceBudget.kt, so a node knows which of its
// announces a transport node will pass on. With transport on, every
// interface gets it by default and it cannot be switched off, only moved:
// the first announce opens the record; each later one sooner than kTargetMs
// after the last one passed is a violation, one later than that forgives
// one, and past kGrace violations an announce updates the node's own path
// table but is not rebroadcast, until kTargetMs after the last one passed.
// So six at once, then one an hour.

#include <stdint.h>

namespace crewradio {
namespace rns {

class AnnounceBudget {
 public:
  // rnsd 1.5.4 with transport on: Interface.DEFAULT_AR_TARGET (3600 s) and
  // DEFAULT_AR_GRACE; its penalty is 0.
  static constexpr int64_t kTargetMs = 3600000;
  static constexpr int kGrace = 5;

  struct Peek {
    bool passes;
    int violations;  // After it
  };

  // What an announce at now would meet, nothing recorded.
  Peek peek(int64_t now) const {
    if (!has_last_) return {true, 0};
    if (now <= blocked_until_) return {false, violations_};
    const int v = now - last_ < kTargetMs
                      ? violations_ + 1
                      : (violations_ > 0 ? violations_ - 1 : 0);
    return {v <= kGrace, v};
  }

  // Records an announce sent at now; true when the transport node passes it
  // on.
  bool record(int64_t now) {
    if (!has_last_) {
      has_last_ = true;
      last_ = now;
      return true;
    }
    if (now <= blocked_until_) return false;
    const Peek p = peek(now);
    violations_ = p.violations;
    if (p.passes) {
      last_ = now;
    } else {
      blocked_until_ = last_ + kTargetMs;
    }
    return p.passes;
  }

  int violations() const { return violations_; }

 private:
  bool has_last_ = false;
  int64_t last_ = 0;
  int violations_ = 0;
  int64_t blocked_until_ = INT64_MIN;
};

}  // namespace rns
}  // namespace crewradio
