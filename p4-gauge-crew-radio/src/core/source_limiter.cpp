// SPDX-License-Identifier: EUPL-1.2
#include "source_limiter.h"

namespace crewradio {

bool SourceLimiter::allow(uint32_t source, int64_t now_ms) {
  Entry* e = nullptr;
  Entry* free_slot = nullptr;
  Entry* oldest = nullptr;
  for (Entry& x : e_) {
    if (!x.used) {
      if (free_slot == nullptr) free_slot = &x;
    } else if (x.source == source) {
      e = &x;
      break;
    } else if (oldest == nullptr || x.stamp < oldest->stamp) {
      oldest = &x;
    }
  }
  if (e == nullptr) {
    e = free_slot != nullptr ? free_slot : oldest;
    e->source = source;
    e->used = true;
    e->milli = c_.burst * 1000;
    e->last_ms = now_ms;
  }
  e->stamp = ++clock_;
  int64_t elapsed = now_ms - e->last_ms;
  if (elapsed < 0) elapsed = 0;
  e->milli += elapsed * c_.per_second;  // per_second tokens a second = per ms x 1000
  if (e->milli > c_.burst * 1000) e->milli = c_.burst * 1000;
  e->last_ms = now_ms;
  if (e->milli < 1000) return false;
  e->milli -= 1000;
  return true;
}

void SourceLimiter::clear() {
  for (Entry& x : e_) x = Entry();
  clock_ = 0;
}

int SourceLimiter::size() const {
  int n = 0;
  for (const Entry& x : e_) n += x.used ? 1 : 0;
  return n;
}

}  // namespace crewradio
