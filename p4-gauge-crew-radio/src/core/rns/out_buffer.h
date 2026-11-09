// SPDX-License-Identifier: EUPL-1.2
#pragma once

// What cr-rns has framed for the transport node and the socket has not taken
// yet (esp/reticulum_link.cpp's writer), with each frame's length, kind and
// time handed over. Voice still waiting kVoiceMaxAgeMs after it was handed
// over is dropped from the buffer before the next send: a socket that stops
// taking bytes for a few seconds and then recovers must not send seconds-old
// speech, which a receiver would still play (its replay window is 60 s).
// A frame the socket has taken part of always goes out whole, or the stream's
// framing breaks: only frames not yet started are dropped. Link control and
// hellos never expire (CodeRabbit's review of PR #17, 2026-11-09). Pure.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace rns {

class OutBuffer {
 public:
  static constexpr size_t kBytes = 16 * 1024;
  static constexpr int kMaxFrames = 256;

  // One framed packet; false (nothing kept) when it does not fit.
  bool append(const uint8_t* frame, size_t len, bool voice, int64_t at_ms);
  const uint8_t* data() const { return buf_; }
  size_t size() const { return len_; }
  int frames() const { return count_; }
  // The socket took n bytes from the front.
  void consume(size_t n);
  // Drops voice not yet started that was handed over more than max_age_ms
  // before now; returns how many frames went.
  int expire(int64_t now_ms, int64_t max_age_ms);
  void clear();

 private:
  struct Frame {
    uint16_t len;
    bool voice;
    int64_t at;
  };
  Frame& at(int i) { return frames_[(head_ + i) % kMaxFrames]; }

  uint8_t buf_[kBytes];
  size_t len_ = 0;
  Frame frames_[kMaxFrames];
  int head_ = 0;
  int count_ = 0;
  size_t front_sent_ = 0;  // Bytes of the first frame the socket has taken
};

}  // namespace rns
}  // namespace crewradio
