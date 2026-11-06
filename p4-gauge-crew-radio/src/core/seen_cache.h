// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The seen-cache: (sender, seq) -> the highest ttl that packet has been
// forwarded with, bounded, the oldest entry evicted first (insertion order,
// as the app's LinkedHashMap; updating a value does not make it younger).
//
// Fixed memory, nothing allocated per packet: an open-addressed table with
// linear probing and backward-shift deletion (no tombstones), twice the
// capacity in slots, and a ring of keys in insertion order for eviction.
// 16384 audio entries take about 640 KB, which the node puts in PSRAM. Only
// authenticated packets are inserted (ingress.h), so nobody without the
// channel key can choose keys to crowd one part of the table.

#include <stddef.h>
#include <stdint.h>

#include <vector>

namespace crewradio {

class SeenCache {
 public:
  explicit SeenCache(size_t capacity);

  // The stored value for `key`, or nullptr.
  uint8_t* find(uint64_t key);
  // Inserts a key that is not present; at capacity the oldest goes first.
  void insert(uint64_t key, uint8_t value);
  size_t size() const { return count_; }
  size_t capacity() const { return capacity_; }

  static uint64_t key(int32_t sender_id, int32_t seq) {
    return ((uint64_t)(uint32_t)sender_id << 32) | (uint32_t)seq;
  }

 private:
  struct Slot {
    uint64_t key;
    uint8_t value;
    bool used;
  };
  size_t home(uint64_t key) const;
  void erase(uint64_t key);

  size_t capacity_;
  std::vector<Slot> slots_;
  size_t mask_;
  std::vector<uint64_t> order_;  // Ring, oldest at head_
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace crewradio
