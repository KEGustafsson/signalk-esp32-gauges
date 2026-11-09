// SPDX-License-Identifier: EUPL-1.2
#include "out_buffer.h"

#include <string.h>

namespace crewradio {
namespace rns {

// Appends one framed packet with its time and kind; false (nothing kept) when it
// does not fit in the bytes or the frame count.
bool OutBuffer::append(const uint8_t* frame, size_t len, bool voice,
                       int64_t at_ms) {
  if (len == 0 || len > 0xFFFF || len_ + len > kBytes ||
      count_ == kMaxFrames) {
    return false;
  }
  memcpy(buf_ + len_, frame, len);
  len_ += len;
  frames_[(head_ + count_) % kMaxFrames] = {(uint16_t)len, voice, at_ms};
  count_++;
  return true;
}

// Takes the bytes the socket accepted off the front, and the frames they
// completed.
void OutBuffer::consume(size_t n) {
  if (n > len_) n = len_;
  memmove(buf_, buf_ + n, len_ - n);
  len_ -= n;
  front_sent_ += n;
  while (count_ > 0 && front_sent_ >= at(0).len) {
    front_sent_ -= at(0).len;
    head_ = (head_ + 1) % kMaxFrames;
    count_--;
  }
}

// Drops voice not yet started that is older than max_age_ms; returns how many
// frames went.
int OutBuffer::expire(int64_t now_ms, int64_t max_age_ms) {
  // Compacts in place, front to back: a kept frame's bytes and record move
  // down over the dropped ones before it (k <= i, so nothing unread is
  // overwritten). A partly sent first frame holds only its unsent bytes in
  // buf_, is always kept and stays first, so front_sent_ stays right.
  size_t read = 0, write = 0;
  int kept = 0;
  const int total = count_;
  for (int i = 0; i < total; i++) {
    const Frame f = at(i);
    const size_t bytes = i == 0 ? f.len - front_sent_ : f.len;
    const bool started = i == 0 && front_sent_ > 0;
    if (!f.voice || started || now_ms - f.at <= max_age_ms) {
      if (write != read) memmove(buf_ + write, buf_ + read, bytes);
      write += bytes;
      frames_[(head_ + kept) % kMaxFrames] = f;
      kept++;
    }
    read += bytes;
  }
  count_ = kept;
  len_ = write;
  return total - kept;
}

void OutBuffer::clear() {
  len_ = 0;
  head_ = 0;
  count_ = 0;
  front_sent_ = 0;
}

}  // namespace rns
}  // namespace crewradio
