// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Ingress budgets, as the app's RateLimiter.kt: a global bucket (5000/s,
// burst 2000) charged before a packet is opened, whatever sender it claims;
// a junk bucket (200/s, burst 400) charged only for packets that fail to
// open; and one bucket per authenticated sender (75/s, burst 150), at most
// 128 senders, idle ones swept after 10 s.
//
// Tokens are counted in thousandths, in integers: the ESP32-P4's FPU is
// single precision and the app's arithmetic is exact in this form (whole
// rates, milliseconds), so the two give the same answers.
//
// Not thread-safe: the node's receive task owns it (one receive task for
// every transport, so the app's locks are not needed).

#include <stdint.h>

#include <vector>

namespace crewradio {

class RateLimiter {
 public:
  struct Config {
    int64_t per_second = 75;
    int64_t burst = 150;
    int64_t global_per_second = 5000;
    int64_t global_burst = 2000;
    int64_t junk_per_second = 200;
    int64_t junk_burst = 400;
    int max_senders = 128;
    int64_t forget_ms = 10000;
  };

  RateLimiter() : RateLimiter(Config()) {}
  explicit RateLimiter(const Config& config);

  bool allow_global(int64_t now_ms);
  bool allow_junk(int64_t now_ms);
  bool allow_sender(int32_t sender_id, int64_t now_ms);

 private:
  struct Bucket {
    int64_t milli;  // Tokens x 1000
    int64_t last_ms;
  };
  struct Sender {
    int32_t id;
    bool used;
    Bucket bucket;
  };
  static bool take(Bucket* b, int64_t now_ms, int64_t rate, int64_t cap);

  Config c_;
  Bucket global_;
  Bucket junk_;
  std::vector<Sender> senders_;
  int count_ = 0;
  int64_t last_sweep_ms_ = 0;
};

}  // namespace crewradio
