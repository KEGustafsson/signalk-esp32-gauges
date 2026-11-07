// SPDX-License-Identifier: EUPL-1.2
#include "rate_limiter.h"

namespace crewradio {

RateLimiter::RateLimiter(const Config& config)
    : c_(config),
      global_{config.global_burst * 1000, 0},
      junk_{config.junk_burst * 1000, 0},
      senders_((size_t)(config.max_senders > 0 ? config.max_senders : 0)) {}

bool RateLimiter::take(Bucket* b, int64_t now_ms, int64_t rate, int64_t cap) {
  int64_t elapsed = now_ms - b->last_ms;
  if (elapsed < 0) elapsed = 0;
  int64_t milli = b->milli + elapsed * rate;  // rate/s x ms = thousandths
  if (milli > cap * 1000) milli = cap * 1000;
  b->milli = milli;
  b->last_ms = now_ms;
  if (b->milli < 1000) return false;
  b->milli -= 1000;
  return true;
}

bool RateLimiter::allow_global(int64_t now_ms) {
  return take(&global_, now_ms, c_.global_per_second, c_.global_burst);
}

bool RateLimiter::allow_junk(int64_t now_ms) {
  return take(&junk_, now_ms, c_.junk_per_second, c_.junk_burst);
}

bool RateLimiter::allow_sender(int32_t sender_id, int64_t now_ms) {
  if (now_ms - last_sweep_ms_ > c_.forget_ms) {
    for (Sender& s : senders_) {
      if (s.used && now_ms - s.bucket.last_ms > c_.forget_ms) {
        s.used = false;
        count_--;
      }
    }
    last_sweep_ms_ = now_ms;
  }
  Sender* free_slot = nullptr;
  for (Sender& s : senders_) {
    if (s.used && s.id == sender_id) {
      return take(&s.bucket, now_ms, c_.per_second, c_.burst);
    }
    if (!s.used && free_slot == nullptr) free_slot = &s;
  }
  if (free_slot == nullptr) return false;  // Table full: no state for a newcomer
  free_slot->used = true;
  free_slot->id = sender_id;
  free_slot->bucket = {c_.burst * 1000, now_ms};
  count_++;
  return take(&free_slot->bucket, now_ms, c_.per_second, c_.burst);
}

}  // namespace crewradio
