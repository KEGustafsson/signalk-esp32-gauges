// SPDX-License-Identifier: EUPL-1.2
#include "talk_key.h"

namespace crewradio {

TalkKey::Event TalkKey::sample(bool button, bool touch, int64_t now_ms) {
  const bool held = button || touch;
  if (held) {
    down_++;
    up_ = 0;
  } else {
    up_++;
    down_ = 0;
  }
  if (locked_) {
    // After a time-out: back only once everything has been let go.
    if (up_ >= kReleaseFrames) locked_ = false;
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
