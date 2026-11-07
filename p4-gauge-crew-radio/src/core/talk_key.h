// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The talk key: push to talk from a button on the 40-pin header and a
// two-finger hold on the screen, sampled once a 20 ms frame. Either keys the
// mic; a press needs kPressFrames in a row and a release kReleaseFrames, so
// a contact's bounce or one missed touch read neither keys nor drops a word.
//
// A time-out, as on a VHF set: kTimeoutMs of continuous talk un-keys the
// mic and it stays un-keyed until every input has been released. A button
// shorted by spray or a hand resting on the screen would otherwise hold
// the channel for the whole crew, and nobody else could be heard. Only the
// inputs themselves being let go ends the lock-out: a moment when talking
// is not allowed (the memory guard, the clock) un-keys like a release but
// does not, or a stuck button would key again after every such moment.
// Pure; owned by the audio task.

#include <stdint.h>

namespace crewradio {

class TalkKey {
 public:
  static constexpr int kPressFrames = 2;    // 40 ms
  static constexpr int kReleaseFrames = 3;  // 60 ms
  static constexpr int64_t kTimeoutMs = 90000;

  enum class Event : uint8_t { kNone, kKeyed, kUnkeyed, kTimedOut };

  // One sample of the inputs as they are, and whether talking is allowed
  // now; the event this sample caused.
  Event sample(bool button, bool touch, bool may_talk, int64_t now_ms);

  bool talking() const { return talking_; }
  // True from a time-out until every input is released.
  bool locked_out() const { return locked_; }

 private:
  bool talking_ = false;
  bool locked_ = false;
  // Consecutive samples keying (held and allowed) and not keying; and with
  // nothing held at all, for the lock-out. Saturating.
  int down_ = 0;
  int up_ = 0;
  int released_ = 0;
  int64_t keyed_ms_ = 0;
};

}  // namespace crewradio
