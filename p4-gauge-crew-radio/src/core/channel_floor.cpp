// SPDX-License-Identifier: EUPL-1.2
#include "channel_floor.h"

namespace crewradio {

// Notes another sender's accepted audio: the channel is busy, and a collision
// is due if we are talking.
void ChannelFloor::heard(int32_t sender, int64_t now_ms) {
  if (sender == self_) return;
  heard_any_ = true;
  last_ = sender;
  last_ms_ = now_ms;
  if (!fresh_ || (uint32_t)sender < (uint32_t)fresh_lowest_) {
    fresh_lowest_ = sender;
  }
  fresh_ = true;
}

// True while another talker was heard within kBusyMs; *who is the last one.
bool ChannelFloor::busy(int64_t now_ms, int32_t* who) const {
  if (!heard_any_ || now_ms - last_ms_ >= kBusyMs) return false;
  if (who != nullptr) *who = last_;
  return true;
}

ChannelFloor::Event ChannelFloor::sample(bool held, bool talking,
                                         int64_t now_ms, bool* may_talk) {
  const bool collided = fresh_;
  const int32_t lowest = fresh_lowest_;
  fresh_ = false;

  released_ = held ? 0 : (released_ < kReleaseFrames ? released_ + 1
                                                       : released_);
  Event event = Event::kNone;
  if (released_ >= kReleaseFrames) {
    state_ = State::kIdle;  // Let go: the next press starts afresh
  } else if (held && state_ == State::kIdle) {
    if (busy(now_ms)) {
      state_ = State::kRefused;
      refused_ms_ = now_ms;
      event = Event::kRefused;
    } else {
      state_ = State::kFree;
    }
  }

  switch (state_) {
    case State::kRefused:
      if (held && now_ms - refused_ms_ >= kOverrideMs) {
        state_ = State::kOverride;
        event = Event::kOverride;
      }
      break;
    case State::kFree:
      if (talking && collided && (uint32_t)lowest < (uint32_t)self_) {
        state_ = State::kYielded;
        event = Event::kYielded;
      }
      break;
    default:
      break;
  }
  *may_talk = state_ == State::kIdle || state_ == State::kFree ||
              state_ == State::kOverride;
  return event;
}

}  // namespace crewradio
