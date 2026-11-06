// SPDX-License-Identifier: EUPL-1.2
#pragma once

// A single-producer, single-consumer ring of fixed-size slots, lock-free:
// how the network task hands admitted audio to the playout task. One task
// writes, one task reads, nothing else touches it; a full ring refuses the
// write (the caller counts the drop) rather than block the network task.
//
// write_slot() / commit() on the producer side, read_slot() / release() on
// the consumer side. The indices are atomics with acquire/release ordering,
// so the slot's bytes are visible before its index moves.

#include <stddef.h>
#include <stdint.h>

#include <atomic>
#include <vector>

namespace crewradio {

template <size_t kSlotBytes>
class SpscRing {
 public:
  // `slots` usable entries (one more is allocated to tell full from empty).
  explicit SpscRing(size_t slots) : slots_(slots + 1) {}

  // The next free slot, or nullptr when the ring is full.
  uint8_t* write_slot() {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t next = (tail + 1) % slots_.size();
    if (next == head_.load(std::memory_order_acquire)) return nullptr;
    return slots_[tail].data;
  }
  // Publishes the slot write_slot() returned, `len` bytes of it.
  void commit(uint16_t len) {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    slots_[tail].len = len;
    tail_.store((tail + 1) % slots_.size(), std::memory_order_release);
  }

  // The oldest published slot, or nullptr when the ring is empty.
  const uint8_t* read_slot(uint16_t* len) {
    const size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) return nullptr;
    *len = slots_[head].len;
    return slots_[head].data;
  }
  // Frees the slot read_slot() returned.
  void release() {
    const size_t head = head_.load(std::memory_order_relaxed);
    head_.store((head + 1) % slots_.size(), std::memory_order_release);
  }

  size_t capacity() const { return slots_.size() - 1; }

 private:
  struct Slot {
    uint16_t len = 0;
    uint8_t data[kSlotBytes];
  };
  std::vector<Slot> slots_;
  std::atomic<size_t> head_{0};  // Consumer's
  std::atomic<size_t> tail_{0};  // Producer's
};

}  // namespace crewradio
