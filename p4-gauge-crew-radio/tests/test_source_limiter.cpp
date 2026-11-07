// SPDX-License-Identifier: EUPL-1.2
// The per-source ingress budget (as the app's SourceLimiterTest), and that a
// keyless flood from one WLAN host no longer silences the crew.
#include "core/source_limiter.h"
#include "test.h"

using namespace crewradio;

TEST(source_a_burst_then_its_rate) {
  SourceLimiter s;
  int passed = 0;
  for (int i = 0; i < 2000; i++) passed += s.allow(0x0A000001, 0) ? 1 : 0;
  CHECK(passed == 1000);  // The burst, no more
  CHECK(!s.allow(0x0A000001, 1));  // 0.5 of a token in 1 ms
  CHECK(s.allow(0x0A000001, 2));   // One token in 2 ms (500/s)
  CHECK(!s.allow(0x0A000001, 2));
}

TEST(source_each_address_has_its_own_budget) {
  SourceLimiter s;
  for (int i = 0; i < 1000; i++) s.allow(1, 0);
  CHECK(!s.allow(1, 0));
  CHECK(s.allow(2, 0));  // Another host is untouched
}

TEST(source_the_table_evicts_the_least_recently_used_never_a_newcomer) {
  SourceLimiter s;
  for (uint32_t a = 1; a <= (uint32_t)SourceLimiter::kMaxSources; a++) {
    CHECK(s.allow(a, 0));
  }
  CHECK(s.size() == SourceLimiter::kMaxSources);
  CHECK(s.allow(1, 1));  // Address 1 used again: 2 is now the oldest
  CHECK(s.allow(1000, 1));  // A newcomer is never refused
  CHECK(s.size() == SourceLimiter::kMaxSources);
  // 2 was evicted: it comes back with a full burst
  int passed = 0;
  for (int i = 0; i < 1500; i++) passed += s.allow(2, 1) ? 1 : 0;
  CHECK(passed == 1000);
}

TEST(source_a_clock_that_steps_back_refills_nothing) {
  SourceLimiter s;
  for (int i = 0; i < 1000; i++) s.allow(7, 10000);
  CHECK(!s.allow(7, 0));  // Back in time: no tokens for it
  CHECK(!s.allow(7, 1));  // Refilled from the step back only
  CHECK(s.allow(7, 2));
}

TEST(source_clear_forgets_everyone) {
  SourceLimiter s;
  for (int i = 0; i < 1000; i++) s.allow(7, 0);
  s.clear();
  CHECK(s.size() == 0);
  CHECK(s.allow(7, 0));
}
