// SPDX-License-Identifier: EUPL-1.2
#include "seen_cache.h"

namespace crewradio {

namespace {

size_t table_size(size_t capacity) {
  size_t n = 16;
  while (n < capacity * 2) n <<= 1;
  return n;
}

}  // namespace

SeenCache::SeenCache(size_t capacity)
    : capacity_(capacity < 1 ? 1 : capacity),
      slots_(table_size(capacity_)),
      mask_(slots_.size() - 1),
      order_(capacity_) {}

size_t SeenCache::home(uint64_t key) const {
  // splitmix64's finaliser: sequence numbers are consecutive, so spread them.
  uint64_t z = key + 0x9E3779B97F4A7C15ull;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  z ^= z >> 31;
  return (size_t)z & mask_;
}

uint8_t* SeenCache::find(uint64_t key) {
  for (size_t i = home(key);; i = (i + 1) & mask_) {
    Slot& s = slots_[i];
    if (!s.used) return nullptr;
    if (s.key == key) return &s.value;
  }
}

void SeenCache::insert(uint64_t key, uint8_t value) {
  if (count_ == capacity_) {
    erase(order_[head_]);
    head_ = (head_ + 1) % capacity_;
    count_--;
  }
  size_t i = home(key);
  while (slots_[i].used) i = (i + 1) & mask_;
  slots_[i] = {key, value, true};
  order_[(head_ + count_) % capacity_] = key;
  count_++;
}

void SeenCache::erase(uint64_t key) {
  size_t i = home(key);
  while (slots_[i].used && slots_[i].key != key) i = (i + 1) & mask_;
  if (!slots_[i].used) return;
  // Backward shift: pull later entries of the run into the hole when their
  // home is at or before it (cyclically), so no lookup ever stops short.
  size_t hole = i;
  for (size_t j = (hole + 1) & mask_; slots_[j].used; j = (j + 1) & mask_) {
    const size_t h = home(slots_[j].key);
    // Can slot j's entry move to `hole`? Only if its home is not strictly
    // between hole and j (cyclically).
    const bool between =
        hole <= j ? (h > hole && h <= j) : (h > hole || h <= j);
    if (!between) {
      slots_[hole] = slots_[j];
      hole = j;
    }
  }
  slots_[hole].used = false;
}

}  // namespace crewradio
