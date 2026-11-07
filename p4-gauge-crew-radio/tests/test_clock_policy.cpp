// SPDX-License-Identifier: EUPL-1.2
// When the wall clock is to be trusted.
#include "core/clock_policy.h"
#include "test.h"

using namespace crewradio;

namespace {
constexpr int64_t kNow = 1788739200;  // 2026-09-07
constexpr int64_t kHour = 3600LL * 1000;
}  // namespace

TEST(clock_untrusted_until_a_source_has_synced) {
  ClockPolicy c;
  CHECK(c.source(0, kNow) == ClockSource::kNone);
  c.sntp_synced(1000);
  CHECK(c.source(2000, kNow) == ClockSource::kSntp);
  // A clock before 2026 is unset whatever synced.
  CHECK(c.source(2000, 1000) == ClockSource::kNone);
}

TEST(clock_a_sync_is_trusted_for_a_day) {
  ClockPolicy c;
  c.sntp_synced(0);
  CHECK(c.source(24 * kHour, kNow) == ClockSource::kSntp);
  CHECK(c.source(24 * kHour + 1, kNow) == ClockSource::kNone);
}

TEST(clock_gnss_steps_a_clock_that_is_off_when_sntp_is_not_fresh) {
  ClockPolicy c;
  CHECK(!c.gnss(kNow, kNow - 30, 0));         // A first reading proves nothing
  CHECK(c.gnss(kNow + 1, kNow - 29, 1000));   // It moved with time: set it
  CHECK(!c.gnss(kNow + 2, kNow + 1, 2000));   // Within 2 s: leave it
  CHECK(c.source(2000, kNow) == ClockSource::kGnss);
  CHECK(!c.gnss(1000, kNow, 3000));           // No fix (1970): ignored
}

TEST(clock_a_retained_or_frozen_gnss_time_never_sets_the_clock) {
  ClockPolicy c;
  // The same value again and again (a dead receiver; Signal K's last value).
  CHECK(!c.gnss(kNow - 3600, kNow, 0));
  CHECK(!c.gnss(kNow - 3600, kNow, 1000));
  CHECK(!c.gnss(kNow - 3600, kNow, 2000));
  CHECK(c.source(2000, kNow) == ClockSource::kNone);
  // Jumping faster than time (a replayed log) does not count either.
  CHECK(!c.gnss(kNow + 100, kNow, 3000));
  CHECK(c.gnss(kNow + 101, kNow, 4000));  // Moving with time again: live
  // After the connection drops, the first reading starts over.
  c.gnss_lost();
  CHECK(!c.gnss(kNow + 200, kNow, 5000));
}

TEST(clock_fresh_sntp_wins_over_gnss) {
  ClockPolicy c;
  c.sntp_synced(0);
  c.gnss(kNow - 31, kNow, 0);
  CHECK(!c.gnss(kNow - 30, kNow, 1000));  // Disagrees: not stepped, not noted
  CHECK(c.source(1000, kNow) == ClockSource::kSntp);
  // An hour later SNTP is no longer fresh: GNSS steps it.
  c.gnss(kNow - 1, kNow - 30, kHour);
  CHECK(c.gnss(kNow, kNow - 29, kHour + 1000));
  CHECK(c.source(kHour + 1000, kNow) == ClockSource::kGnss);
}

TEST(clock_parses_signal_k_datetime) {
  int64_t t = 0;
  CHECK(parse_iso8601_utc("2026-09-07T00:00:00Z", &t) && t == kNow);
  CHECK(parse_iso8601_utc("2026-09-07T00:00:00.250Z", &t) && t == kNow);
  CHECK(parse_iso8601_utc("2026-09-07T03:00:00+03:00", &t) && t == kNow);
  CHECK(parse_iso8601_utc("2026-09-06T23:30:00-00:30", &t) && t == kNow);
  CHECK(parse_iso8601_utc("2000-02-29T12:00:00Z", &t) && t == 951825600);
  CHECK(!parse_iso8601_utc("2026-09-07T00:00:00", &t));   // No zone: not UTC
  CHECK(!parse_iso8601_utc("2026-13-07T00:00:00Z", &t));
  CHECK(!parse_iso8601_utc("2026-02-31T00:00:00Z", &t));  // Not March 3rd
  CHECK(!parse_iso8601_utc("2026-04-31T00:00:00Z", &t));
  CHECK(!parse_iso8601_utc("2026-02-29T00:00:00Z", &t));  // Not a leap year
  CHECK(!parse_iso8601_utc("1900-02-29T00:00:00Z", &t));  // Nor a century
  CHECK(parse_iso8601_utc("2028-02-29T00:00:00Z", &t));
  CHECK(parse_iso8601_utc("2026-12-31T23:59:59Z", &t));
  CHECK(!parse_iso8601_utc("2026-09-07 00:00:00Z", &t));
  CHECK(!parse_iso8601_utc("2026-09-07T25:00:00Z", &t));
  CHECK(!parse_iso8601_utc("", &t));
  CHECK(!parse_iso8601_utc(nullptr, &t));
}
