// SPDX-License-Identifier: EUPL-1.2
// The lock-free hand-over between the network and playout tasks.
#include <string.h>

#include <thread>

#include "core/spsc_ring.h"
#include "test.h"

using namespace crewradio;

TEST(ring_holds_its_capacity_in_order) {
  SpscRing<8> r(3);
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t* s = r.write_slot();
    CHECK(s != nullptr);
    s[0] = i;
    r.commit(1);
  }
  CHECK(r.write_slot() == nullptr);  // Full: refused, not blocked
  uint16_t len = 0;
  for (uint8_t i = 0; i < 3; i++) {
    const uint8_t* s = r.read_slot(&len);
    CHECK(s != nullptr && len == 1 && s[0] == i);
    r.release();
  }
  CHECK(r.read_slot(&len) == nullptr);
}

TEST(ring_two_threads_lose_and_reorder_nothing) {
  SpscRing<16> r(64);
  constexpr uint32_t kCount = 200000;
  std::thread producer([&] {
    for (uint32_t i = 0; i < kCount;) {
      uint8_t* s = r.write_slot();
      if (s == nullptr) {
        std::this_thread::yield();
        continue;
      }
      memcpy(s, &i, sizeof(i));
      memset(s + 4, (int)(i & 0xFF), 12);
      r.commit(16);
      i++;
    }
  });
  uint32_t expected = 0;
  bool ok = true;
  while (expected < kCount) {
    uint16_t len = 0;
    const uint8_t* s = r.read_slot(&len);
    if (s == nullptr) {
      std::this_thread::yield();
      continue;
    }
    uint32_t v;
    memcpy(&v, s, sizeof(v));
    ok = ok && v == expected && len == 16 && s[15] == (uint8_t)(v & 0xFF);
    r.release();
    expected++;
  }
  producer.join();
  CHECK(ok);
}
