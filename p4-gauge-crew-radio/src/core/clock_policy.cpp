// SPDX-License-Identifier: EUPL-1.2
#include "clock_policy.h"

namespace crewradio {

namespace {

bool digits(const char* p, int n, int* out) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (p[i] < '0' || p[i] > '9') return false;
    v = v * 10 + (p[i] - '0');
  }
  *out = v;
  return true;
}

// Days from 1970-01-01 to y-m-d (proleptic Gregorian; Hinnant's algorithm).
int64_t days_from_civil(int64_t y, int m, int d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

}  // namespace

bool parse_iso8601_utc(const char* t, int64_t* epoch_s) {
  if (t == nullptr) return false;
  int y, mo, d, h, mi, s;
  if (!digits(t, 4, &y) || t[4] != '-' || !digits(t + 5, 2, &mo) ||
      t[7] != '-' || !digits(t + 8, 2, &d) || t[10] != 'T' ||
      !digits(t + 11, 2, &h) || t[13] != ':' || !digits(t + 14, 2, &mi) ||
      t[16] != ':' || !digits(t + 17, 2, &s)) {
    return false;
  }
  if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 60) {
    return false;
  }
  const char* p = t + 19;
  if (*p == '.') {
    p++;
    if (*p < '0' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') p++;
  }
  int64_t offset_s = 0;
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    int oh, om;
    if (!digits(p + 1, 2, &oh) || p[3] != ':' || !digits(p + 4, 2, &om) ||
        oh > 23 || om > 59) {
      return false;
    }
    offset_s = (int64_t)(oh * 3600 + om * 60) * (*p == '+' ? 1 : -1);
    p += 6;
  } else {
    return false;
  }
  if (*p != '\0') return false;
  *epoch_s = days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + s -
             offset_s;
  return true;
}

void ClockPolicy::sntp_synced(int64_t mono_ms) {
  has_sntp_ = true;
  sntp_ms_ = mono_ms;
}

bool ClockPolicy::gnss(int64_t gnss_s, int64_t system_s, int64_t mono_ms) {
  if (gnss_s < kMinEpochS) return false;  // A receiver without a fix
  // Live only if it advanced in step with the monotonic clock since the
  // last reading: a retained or frozen value does not.
  const bool had = has_reading_;
  const int64_t ds_ms = (gnss_s - reading_s_) * 1000;
  const int64_t dm_ms = mono_ms - reading_ms_;
  has_reading_ = true;
  reading_s_ = gnss_s;
  reading_ms_ = mono_ms;
  if (!had || dm_ms < 500 || dm_ms > 60000 || ds_ms <= 0) return false;
  const int64_t skew = ds_ms > dm_ms ? ds_ms - dm_ms : dm_ms - ds_ms;
  if (skew > kStepS * 1000) return false;
  const int64_t off = gnss_s > system_s ? gnss_s - system_s : system_s - gnss_s;
  if (recent(has_sntp_, sntp_ms_, mono_ms, kSntpPreferMs)) {
    // SNTP is fresh: GNSS only confirms it, and confirms only when it agrees.
    if (off <= kStepS) {
      has_gnss_ = true;
      gnss_ms_ = mono_ms;
    }
    return false;
  }
  has_gnss_ = true;
  gnss_ms_ = mono_ms;
  return off > kStepS;
}

ClockSource ClockPolicy::source(int64_t mono_ms, int64_t system_s) const {
  if (system_s < kMinEpochS) return ClockSource::kNone;
  const bool sntp = recent(has_sntp_, sntp_ms_, mono_ms, kTrustMs);
  const bool gnss = recent(has_gnss_, gnss_ms_, mono_ms, kTrustMs);
  if (sntp && gnss) {
    return sntp_ms_ >= gnss_ms_ ? ClockSource::kSntp : ClockSource::kGnss;
  }
  if (sntp) return ClockSource::kSntp;
  if (gnss) return ClockSource::kGnss;
  return ClockSource::kNone;
}

}  // namespace crewradio
