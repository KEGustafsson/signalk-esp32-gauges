// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Per-sender decoders, as the app's engine: a decoder keeps state between
// frames, so each talker needs its own; at most kMax at once, the quietest
// (least recently used) taken over when a new talker needs one, but only if
// it has been silent kEvictIdleMs (else the newcomer's frame is dropped: a
// ninth talker must not garble all eight by resetting a live decoder every
// frame), and released after kIdleMs of silence. The decoders are made once, up front (on the
// node each Opus decoder is about 18 KB, in PSRAM) and reset when they
// change hands, so nothing is allocated while audio flows. Owned by the
// playout task.

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "audio_config.h"

namespace crewradio {

// One 20 ms frame in, kFrameSamples out.
class FrameDecoder {
 public:
  virtual ~FrameDecoder() = default;
  // Samples written (kFrameSamples) or -1.
  virtual int decode(const uint8_t* data, size_t len,
                     int16_t out[audio::kFrameSamples]) = 0;
  virtual void reset() = 0;
};

class DecoderPool {
 public:
  static constexpr size_t kMax = 8;
  static constexpr int64_t kIdleMs = 30000;
  static constexpr int64_t kEvictIdleMs = 2000;  // The app's EVICT_IDLE_NS

  explicit DecoderPool(std::vector<std::unique_ptr<FrameDecoder>> decoders)
      : slots_(decoders.size()) {
    for (size_t i = 0; i < decoders.size(); i++) {
      slots_[i].decoder = std::move(decoders[i]);
    }
  }

  // The decoder for `sender`, taking over the quietest one if none is free
  // and it has paused; nullptr when every one is in use by a talker heard
  // within kEvictIdleMs (or for a pool made without decoders).
  FrameDecoder* get(int32_t sender, int64_t now_ms) {
    Slot* free_slot = nullptr;
    Slot* quietest = nullptr;
    for (Slot& s : slots_) {
      if (s.used && s.sender == sender) {
        s.last_ms = now_ms;
        return s.decoder.get();
      }
      if (!s.used) {
        if (free_slot == nullptr) free_slot = &s;
      } else if (quietest == nullptr || s.last_ms < quietest->last_ms) {
        quietest = &s;
      }
    }
    Slot* slot = free_slot;
    if (slot == nullptr && quietest != nullptr &&
        now_ms - quietest->last_ms >= kEvictIdleMs) {
      slot = quietest;
    }
    if (slot == nullptr) return nullptr;
    slot->decoder->reset();  // Someone else's state is not ours
    slot->used = true;
    slot->sender = sender;
    slot->last_ms = now_ms;
    return slot->decoder.get();
  }

  // Releases decoders silent for kIdleMs.
  void sweep(int64_t now_ms) {
    for (Slot& s : slots_) {
      if (s.used && now_ms - s.last_ms > kIdleMs) s.used = false;
    }
  }

  size_t in_use() const {
    size_t n = 0;
    for (const Slot& s : slots_) n += s.used ? 1 : 0;
    return n;
  }

 private:
  struct Slot {
    std::unique_ptr<FrameDecoder> decoder;
    int32_t sender = 0;
    int64_t last_ms = 0;
    bool used = false;
  };
  std::vector<Slot> slots_;
};

}  // namespace crewradio
