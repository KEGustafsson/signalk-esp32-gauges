// SPDX-License-Identifier: EUPL-1.2
#include "seq_tracker.h"

namespace crewradio {

SeqTracker::SeqTracker(size_t capacity)
    : entries_(capacity < 1 ? 1 : capacity) {}

int SeqTracker::admit(int32_t sender_id, int32_t seq) {
  clock_++;
  Entry* free_slot = nullptr;
  Entry* oldest = nullptr;
  for (Entry& e : entries_) {
    if (e.used && e.id == sender_id) {
      e.stamp = clock_;  // A lookup counts as heard, as in the app
      const int32_t distance = (int32_t)((uint32_t)seq - (uint32_t)e.last);
      if (distance <= 0) return -1;
      e.last = seq;
      return distance - 1;
    }
    if (!e.used) {
      if (free_slot == nullptr) free_slot = &e;
    } else if (oldest == nullptr || e.stamp < oldest->stamp) {
      oldest = &e;
    }
  }
  Entry* slot = free_slot != nullptr ? free_slot : oldest;
  *slot = {sender_id, seq, clock_, true};
  return 0;
}

}  // namespace crewradio
