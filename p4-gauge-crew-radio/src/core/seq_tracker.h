// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Per-sender sequence admission, as the app's SeqTracker.kt: given the
// sequence number of a packet that just arrived, how many went missing
// before it (0 for the next one or the first heard), or -1 for a late or
// repeated one. Wrap-aware (signed 32-bit distance). The high-water marks
// live for the process, not the session: leaving and rejoining the channel
// must not let a recording of a sender's last packets back in. Only the
// least recently heard sender is forgotten, past `capacity` senders.
//
// Not thread-safe: owned by the receive task.

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace crewradio {

class SeqTracker {
 public:
  static constexpr size_t kDefaultCapacity = 256;

  explicit SeqTracker(size_t capacity = kDefaultCapacity);

  int admit(int32_t sender_id, int32_t seq);

 private:
  struct Entry {
    int32_t id;
    int32_t last;
    uint64_t stamp;  // Last access, for least-recently-heard eviction
    bool used;
  };
  std::vector<Entry> entries_;
  uint64_t clock_ = 0;
};

}  // namespace crewradio
