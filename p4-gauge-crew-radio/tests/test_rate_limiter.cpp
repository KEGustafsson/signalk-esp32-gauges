// SPDX-License-Identifier: EUPL-1.2
// The budgets, as the app's RateLimiterTest.
#include "core/rate_limiter.h"
#include "test.h"

using namespace crewradio;

TEST(rate_a_normal_talker_is_never_throttled) {
  RateLimiter r;
  // 50 audio frames and one hello a second, for a minute.
  for (int64_t t = 0; t < 60000; t += 20) {
    CHECK(r.allow_global(t) && r.allow_sender(1, t));
    if (t % 1000 == 0) CHECK(r.allow_sender(1, t));
  }
}

TEST(rate_a_flood_is_cut_to_the_budget) {
  RateLimiter r;
  int passed = 0;
  // 1000 packets in one millisecond: the burst of 150, then nothing.
  for (int i = 0; i < 1000; i++) passed += r.allow_sender(1, 1000) ? 1 : 0;
  CHECK(passed == 150);
  // A second later 75 more.
  passed = 0;
  for (int i = 0; i < 1000; i++) passed += r.allow_sender(1, 2000) ? 1 : 0;
  CHECK(passed == 75);
}

TEST(rate_senders_are_independent) {
  RateLimiter r;
  for (int i = 0; i < 150; i++) r.allow_sender(1, 0);
  CHECK(!r.allow_sender(1, 0));
  CHECK(r.allow_sender(2, 0));
}

TEST(rate_rotating_sender_ids_cannot_exceed_the_global_budget) {
  RateLimiter r;
  int passed = 0;
  for (int i = 0; i < 10000; i++) passed += r.allow_global(0) ? 1 : 0;
  CHECK(passed == 2000);
  CHECK(!r.allow_global(0));
  CHECK(r.allow_global(1));  // 5 a millisecond come back
}

TEST(rate_the_sender_table_is_bounded) {
  RateLimiter r;
  for (int id = 0; id < 128; id++) CHECK(r.allow_sender(id, 0));
  CHECK(!r.allow_sender(1000, 0));  // Full: no state for a newcomer
  CHECK(r.allow_sender(5, 0));      // The known ones carry on
}

TEST(rate_an_idle_sender_is_forgotten_and_starts_fresh) {
  RateLimiter r;
  for (int id = 0; id < 128; id++) r.allow_sender(id, 0);
  for (int i = 0; i < 149; i++) r.allow_sender(7, 0);
  CHECK(!r.allow_sender(7, 0));
  // Past forget_ms the table is swept: a newcomer fits, 7 starts with a full burst.
  CHECK(r.allow_sender(1000, 10001));
  int passed = 0;
  for (int i = 0; i < 200; i++) passed += r.allow_sender(7, 10001) ? 1 : 0;
  CHECK(passed == 150);
}

TEST(rate_junk_has_its_own_budget) {
  RateLimiter r;
  int passed = 0;
  for (int i = 0; i < 1000; i++) passed += r.allow_junk(0) ? 1 : 0;
  CHECK(passed == 400);
  CHECK(r.allow_global(0));  // Charged nothing else
  CHECK(r.allow_sender(1, 0));
}
