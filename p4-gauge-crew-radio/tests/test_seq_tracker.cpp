// SPDX-License-Identifier: EUPL-1.2
// Sequence admission, as the app's SeqTrackerTest.
#include <limits.h>

#include "core/seq_tracker.h"
#include "test.h"

using namespace crewradio;

TEST(seq_in_order_has_no_gap_and_missing_numbers_are_the_gap) {
  SeqTracker t;
  CHECK(t.admit(1, 10) == 0);  // The first heard
  CHECK(t.admit(1, 11) == 0);
  CHECK(t.admit(1, 14) == 2);
}

TEST(seq_late_and_duplicate_are_refused_without_moving_back) {
  SeqTracker t;
  t.admit(1, 10);
  t.admit(1, 12);
  CHECK(t.admit(1, 11) == -1);
  CHECK(t.admit(1, 12) == -1);
  CHECK(t.admit(1, 13) == 0);
}

TEST(seq_senders_are_independent_and_the_counter_wraps) {
  SeqTracker t;
  t.admit(1, 5);
  CHECK(t.admit(2, 100) == 0);
  CHECK(t.admit(1, 6) == 0);
  t.admit(3, INT_MAX);
  CHECK(t.admit(3, INT_MIN) == 0);  // A distance of one
}

TEST(seq_only_the_least_recently_heard_sender_is_evicted) {
  SeqTracker t(3);
  t.admit(1, 10);
  t.admit(2, 10);
  t.admit(3, 10);
  t.admit(1, 11);  // 1 heard again: 2 is now the least recent
  t.admit(4, 10);  // Evicts 2
  CHECK(t.admit(1, 11) == -1);  // 1 remembered: a replay refused
  CHECK(t.admit(3, 10) == -1);
  CHECK(t.admit(2, 10) == 0);   // 2 forgotten: heard as new
}
