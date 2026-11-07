// SPDX-License-Identifier: EUPL-1.2
#include "talk_key.h"

namespace crewradio {

namespace {
constexpr int kSaturate = 1 << 20;
// One more sample of a run, up to kSaturate (a button held for weeks).
int more(int run) { return run < kSaturate ? run + 1 : run; }
}  // namespace

TalkKey::Event TalkKey::sample(bool button, bool touch, bool may_talk,
                               int64_t now_ms) {
  const bool held = button || touch;
  const bool keying = held && may_talk;
  down_ = keying ? more(down_) : 0;
  up_ = keying ? 0 : more(up_);
  released_ = held ? 0 : more(released_);
  if (locked_) {
    // After a time-out: back only once everything has been let go.
    if (released_ >= kReleaseFrames) locked_ = false;
    return Event::kNone;
  }
  if (!talking_) {
    if (down_ >= kPressFrames) {
      talking_ = true;
      keyed_ms_ = now_ms;
      return Event::kKeyed;
    }
    return Event::kNone;
  }
  if (now_ms - keyed_ms_ >= kTimeoutMs) {
    talking_ = false;
    locked_ = true;
    return Event::kTimedOut;
  }
  if (up_ >= kReleaseFrames) {
    talking_ = false;
    return Event::kUnkeyed;
  }
  return Event::kNone;
}

}  // namespace crewradio
