// SPDX-License-Identifier: EUPL-1.2
// The bounded, insertion-ordered seen-cache.
#include <random>
#include <unordered_map>
#include <deque>

#include "core/seen_cache.h"
#include "test.h"

using namespace crewradio;

TEST(seen_finds_what_was_inserted_and_updates_in_place) {
  SeenCache c(4);
  CHECK(c.find(SeenCache::key(1, 1)) == nullptr);
  c.insert(SeenCache::key(1, 1), 3);
  uint8_t* v = c.find(SeenCache::key(1, 1));
  CHECK(v != nullptr && *v == 3);
  *v = 5;
  CHECK(*c.find(SeenCache::key(1, 1)) == 5);
  CHECK(c.find(SeenCache::key(2, 1)) == nullptr);  // Sender in the key
  CHECK(c.find(SeenCache::key(1, 2)) == nullptr);
}

TEST(seen_evicts_the_oldest_and_an_update_does_not_rejuvenate) {
  SeenCache c(3);
  c.insert(1, 1);
  c.insert(2, 2);
  c.insert(3, 3);
  *c.find(1) = 9;  // As LinkedHashMap's put on an existing key: same place
  c.insert(4, 4);
  CHECK(c.find(1) == nullptr);
  CHECK(c.find(2) && c.find(3) && c.find(4));
  CHECK(c.size() == 3);
}

TEST(seen_keys_cover_negative_ids_and_seqs) {
  CHECK(SeenCache::key(-1, 0) != SeenCache::key(0, -1));
  CHECK(SeenCache::key(0, -1) == 0xFFFFFFFFull);
}

TEST(seen_matches_a_reference_model_under_churn) {
  // Random inserts and lookups against an unordered_map + deque model: the
  // backward-shift deletion must never lose an entry or keep an evicted one.
  const size_t cap = 300;
  SeenCache c(cap);
  std::unordered_map<uint64_t, uint8_t> model;
  std::deque<uint64_t> order;
  std::mt19937_64 rng(42);
  for (int i = 0; i < 200000; i++) {
    // A narrow key space so lookups hit and clusters form.
    const uint64_t k = rng() % 2000;
    const bool present = model.count(k) != 0;
    CHECK((c.find(k) != nullptr) == present);
    if (!present) {
      if (order.size() == cap) {
        model.erase(order.front());
        order.pop_front();
      }
      const uint8_t v = (uint8_t)(rng() & 0xFF);
      c.insert(k, v);
      model[k] = v;
      order.push_back(k);
    } else {
      CHECK(*c.find(k) == model[k]);
    }
    if (test::failures > 0) return;  // One message, not thousands
  }
  CHECK(c.size() == cap);
}
