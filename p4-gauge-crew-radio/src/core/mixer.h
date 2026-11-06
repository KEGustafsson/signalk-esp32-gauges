// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Per-sender jitter queues summed into one 20 ms slot at a time, as the
// app's audio/Mixer.kt: a stream waits for two frames (40 ms) before it
// plays, holds at most ten (200 ms, the oldest dropped beyond), and a stream
// idle for a second is swept. A slot the engine knows is lost (a gap in the
// sender's sequence, conceal()) and a queue that runs dry while its sender
// is still talking (a frame within 150 ms) are filled by conceal::frame();
// past its three slots the stream rests and prefills again. With n talkers
// the sum is scaled by 1/sqrt(n) (Q15), then by the gain; cue tones are
// added after both, so a muted node still hears its own key beeps.
//
// Single-threaded by design: on the node every call comes from the playout
// task, which also owns the Opus decoders (the receive task hands it sealed
// packets through a ring), so there is nothing to lock. No allocation after
// construction: kMaxStreams streams of kMaxQueued frames, about 110 KB.

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "audio_config.h"

namespace crewradio {

class Mixer {
 public:
  static constexpr int kMaxStreams = 16;
  static constexpr int kPrefillFrames = 2;
  static constexpr int kMaxQueued = 10;
  static constexpr int kMaxCues = 64;  // 1.28 s of tone
  static constexpr int64_t kIdleMs = 1000;
  static constexpr int64_t kStillTalkingMs = 150;

  Mixer();

  // A new session: every stream and cue dropped, the counter reset.
  void reset();

  // A decoded frame for sender_id's queue. False when every stream slot is
  // taken by someone else (kMaxStreams talking at once).
  bool push(int32_t sender_id, const int16_t frame[audio::kFrameSamples],
            int64_t now_ms);

  // Reserves `count` slots for frames known lost, so timing holds; not for a
  // stream at rest (nothing queued between bursts: nothing to repeat). Slots
  // already concealed because the queue ran dry stood in for those frames, so
  // they are not reserved twice (the app reserves them again, which holds the
  // next frame back by as many slots, the last of them silent).
  void conceal(int32_t sender_id, int count);

  // A cue tone frame, played from the next slot over whatever is sounding.
  void cue(const int16_t frame[audio::kFrameSamples]);

  // One slot into `out`. Returns how many sources sounded (streams + cue).
  int tick(int64_t now_ms, int16_t out[audio::kFrameSamples]);

  // Gain on the summed speech, 0..1 (the mute is 0).
  void set_gain(float gain);
  // Silence over everything, cues included (the channel is on hold).
  void set_muted(bool muted) { muted_ = muted; }

  uint64_t concealed_frames() const { return concealed_frames_; }
  int stream_count() const;

 private:
  struct Stream {
    bool used = false;
    int32_t id = 0;
    int64_t last_seen_ms = 0;
    bool primed = false;
    bool has_last = false;
    int concealed = 0;
    int dry = 0;  // Slots concealed on an empty queue since the last frame
    int head = 0;
    int size = 0;
    bool hole[kMaxQueued] = {};
    int16_t frames[kMaxQueued][audio::kFrameSamples];
    int16_t last[audio::kFrameSamples];
  };
  Stream* find(int32_t sender_id);
  void enqueue(Stream* st, const int16_t* frame);  // nullptr = a hole
  const int16_t* next_frame(Stream* st, int64_t now_ms);
  static void rest(Stream* st);

  std::vector<Stream> streams_;
  int16_t fill_[audio::kFrameSamples];
  int32_t acc_[audio::kFrameSamples];
  std::vector<int16_t> cues_;  // kMaxCues frames, a ring
  int cue_head_ = 0;
  int cue_size_ = 0;
  int32_t gain_q15_ = 32768;
  bool muted_ = false;
  uint64_t concealed_frames_ = 0;
};

}  // namespace crewradio
