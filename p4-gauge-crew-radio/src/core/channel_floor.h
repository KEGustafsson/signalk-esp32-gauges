// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Channel busy, as the app's and the plugin's (crew-radio's "radio under
// load" rules, 2026-11-08), without a message or a wire change. Two crew
// talking at once do not hear each other (half duplex) and double the load
// on every link, so:
//
//  1. Listen before talk: a key pressed while another talker is heard
//     (their audio within kBusyMs) does not key. Busy cue; the key stays
//     held but silent. Still held kOverrideMs after the cue, it keys anyway
//     as an override, which rule 2 leaves alone.
//  2. A collision (both keyed within the network's delay): while talking,
//     audio from a talker whose id is lower, as an unsigned 32-bit number,
//     than ours makes us yield: the mic goes off at once, busy cue, and
//     stays off until the key is let go. Both ends compare the same two
//     ids, so exactly one of them keeps the channel on every node, whatever
//     the latency.
//  3. Let go and pressed again: rule 1 again.
//
// The talk key (talk_key.h) still debounces and times out; this only says
// whether it may key. Pure; owned by the audio task, sampled once a frame.

#include <stdint.h>

namespace crewradio {

class ChannelFloor {
 public:
  static constexpr int64_t kBusyMs = 400;  // The bursts' gap, as the app's
  static constexpr int64_t kOverrideMs = 1000;
  static constexpr int kReleaseFrames = 3;  // As the talk key: 60 ms

  enum class Event : uint8_t {
    kNone,
    kRefused,   // Pressed while busy: busy cue
    kOverride,  // Held through the busy cue: keys anyway
    kYielded,   // Lost a collision: busy cue, mic off until let go
  };

  explicit ChannelFloor(int32_t self) : self_(self) {}

  // Accepted audio from another sender (not a copy, not our own).
  void heard(int32_t sender, int64_t now_ms);
  // Another talker heard within kBusyMs; *who is the last one.
  bool busy(int64_t now_ms, int32_t* who = nullptr) const;

  // One frame: the inputs held (button or touch) and whether the talk key
  // is keyed now. *may_talk is what the talk key may do this frame.
  Event sample(bool held, bool talking, int64_t now_ms, bool* may_talk);

  // The key is held but kept silent (refused or yielded): the screen says
  // "Channel busy".
  bool holding_off() const {
    return state_ == State::kRefused || state_ == State::kYielded;
  }

 private:
  enum class State : uint8_t { kIdle, kFree, kRefused, kOverride, kYielded };

  const int32_t self_;
  State state_ = State::kIdle;
  int released_ = kReleaseFrames;
  int64_t refused_ms_ = 0;
  bool heard_any_ = false;
  int32_t last_ = 0;
  int64_t last_ms_ = 0;
  // A talker heard since the last sample, for rule 2.
  bool fresh_ = false;
  int32_t fresh_lowest_ = 0;
};

}  // namespace crewradio
