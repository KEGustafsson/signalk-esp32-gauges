// SPDX-License-Identifier: EUPL-1.2
#include "mixer.h"

#include <math.h>
#include <string.h>

#include "conceal.h"

namespace crewradio {

namespace {

constexpr size_t kSamples = audio::kFrameSamples;

// 1/sqrt(n) in Q15 for n sources at once, 0 and 1 unity; more than the last
// entry share its scale. The app's HEADROOM_Q15, rounded the same way.
struct Headroom {
  int32_t q15[17];
  Headroom() {
    for (int n = 0; n < 17; n++) {
      q15[n] = n <= 1 ? 32768 : (int32_t)lround(32768.0 / sqrt((double)n));
    }
  }
};
const Headroom kHeadroom;

}  // namespace

Mixer::Mixer() : streams_(kMaxStreams), cues_((size_t)kMaxCues * kSamples) {}

void Mixer::reset() {
  for (Stream& st : streams_) st.used = false;
  cue_head_ = cue_size_ = 0;
  concealed_frames_ = 0;
}

void Mixer::set_gain(float gain) {
  if (!(gain >= 0.0f)) gain = 0.0f;  // NaN too
  if (gain > 1.0f) gain = 1.0f;
  gain_q15_ = (int32_t)(gain * 32768.0f);
}

int Mixer::stream_count() const {
  int n = 0;
  for (const Stream& st : streams_) n += st.used ? 1 : 0;
  return n;
}

Mixer::Stream* Mixer::find(int32_t sender_id) {
  for (Stream& st : streams_) {
    if (st.used && st.id == sender_id) return &st;
  }
  return nullptr;
}

void Mixer::enqueue(Stream* st, const int16_t* frame) {
  if (st->size == kMaxQueued) {  // Drop the oldest beyond the cap
    st->head = (st->head + 1) % kMaxQueued;
    st->size--;
  }
  const int at = (st->head + st->size) % kMaxQueued;
  st->hole[at] = frame == nullptr;
  if (frame != nullptr) memcpy(st->frames[at], frame, sizeof(st->frames[at]));
  st->size++;
}

bool Mixer::push(int32_t sender_id, const int16_t frame[kSamples],
                 int64_t now_ms) {
  Stream* st = find(sender_id);
  if (st == nullptr) {
    for (Stream& s : streams_) {
      if (!s.used) {
        st = &s;
        break;
      }
    }
    if (st == nullptr) return false;
    st->used = true;
    st->id = sender_id;
    st->head = st->size = 0;
    rest(st);
  }
  st->last_seen_ms = now_ms;
  enqueue(st, frame);
  return true;
}

void Mixer::conceal(int32_t sender_id, int count) {
  Stream* st = find(sender_id);
  if (st == nullptr || (!st->primed && st->size == 0)) return;
  if (count > conceal::kMaxFrames) count = conceal::kMaxFrames;
  for (int i = 0; i < count; i++) enqueue(st, nullptr);
}

void Mixer::cue(const int16_t frame[kSamples]) {
  if (cue_size_ == kMaxCues) return;
  const int at = (cue_head_ + cue_size_) % kMaxCues;
  memcpy(&cues_[(size_t)at * kSamples], frame, kSamples * sizeof(int16_t));
  cue_size_++;
}

void Mixer::rest(Stream* st) {
  st->primed = false;
  st->concealed = 0;
  st->has_last = false;
}

const int16_t* Mixer::next_frame(Stream* st, int64_t now_ms) {
  if (!st->primed && st->size >= kPrefillFrames) st->primed = true;
  if (!st->primed) return nullptr;
  bool hole = false;
  if (st->size > 0) {
    const int at = st->head;
    st->head = (st->head + 1) % kMaxQueued;
    st->size--;
    if (!st->hole[at]) {
      memcpy(st->last, st->frames[at], sizeof(st->last));
      st->has_last = true;
      st->concealed = 0;
      return st->last;
    }
    hole = true;
  }
  // A known hole, or a queue that ran dry while the sender is still talking.
  if (!hole && now_ms - st->last_seen_ms > kStillTalkingMs) {
    rest(st);
    return nullptr;
  }
  if (!conceal::frame(st->has_last ? st->last : nullptr, st->concealed + 1,
                      fill_)) {
    if (!hole) rest(st);  // Dry past what concealment covers: a pause
    return nullptr;
  }
  st->concealed++;
  concealed_frames_++;
  return fill_;
}

int Mixer::tick(int64_t now_ms, int16_t out[kSamples]) {
  memset(acc_, 0, sizeof(acc_));
  int active = 0;
  for (Stream& st : streams_) {
    if (!st.used) continue;
    const int16_t* frame = next_frame(&st, now_ms);
    if (frame != nullptr) {
      active++;
      for (size_t i = 0; i < kSamples; i++) acc_[i] += frame[i];
    } else if (now_ms - st.last_seen_ms > kIdleMs) {
      st.used = false;  // Swept: the next frame starts a new stream
    }
  }
  // The gain and the crowd headroom scale the speech only.
  if (active > 0) {
    const int idx = active < 16 ? active : 16;
    const int32_t g =
        (int32_t)(((int64_t)gain_q15_ * kHeadroom.q15[idx]) >> 15);
    if (g != 32768) {
      for (size_t i = 0; i < kSamples; i++) {
        acc_[i] = (int32_t)(((int64_t)acc_[i] * g) >> 15);
      }
    }
  }
  if (cue_size_ > 0) {
    const int16_t* cue = &cues_[(size_t)cue_head_ * kSamples];
    cue_head_ = (cue_head_ + 1) % kMaxCues;
    cue_size_--;
    active++;
    for (size_t i = 0; i < kSamples; i++) acc_[i] += cue[i];
  }
  if (active == 0 || muted_) {
    memset(out, 0, kSamples * sizeof(int16_t));
  } else {
    for (size_t i = 0; i < kSamples; i++) {
      const int32_t v = acc_[i];
      out[i] = (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
    }
  }
  return active;
}

}  // namespace crewradio
