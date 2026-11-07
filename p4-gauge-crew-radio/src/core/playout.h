// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The playout task's side of a received frame: the network task has
// admitted it (ingress.h) and handed it over (spsc_ring.h) with the gap
// before it; here the gap is reserved in the mixer as lost slots, the frame
// decoded by its sender's decoder (PCM is copied), and pushed. Every call
// from the playout task, which owns the mixer and the decoders.

#include <stddef.h>
#include <stdint.h>

#include "decoder_pool.h"
#include "mixer.h"
#include "packet.h"

namespace crewradio {

// What the network task puts in the ring: this header, then `len` bytes.
struct Delivery {
  int32_t sender;
  uint8_t codec;  // Codec::kPcm or Codec::kOpus
  uint8_t pad;
  int16_t gap;    // Frames lost before this one (0..)
  uint16_t len;
  uint16_t pad2;
};

class Playout {
 public:
  struct Counters {
    uint64_t frames = 0;
    uint64_t decode_errors = 0;
    uint64_t refused = 0;  // No mixer stream free
  };

  Playout(Mixer* mixer, DecoderPool* decoders)
      : mixer_(mixer), decoders_(decoders) {}

  // One delivery: the header and its payload.
  void deliver(const Delivery& d, const uint8_t* payload, int64_t now_ms);

  const Counters& counters() const { return c_; }

 private:
  Mixer* mixer_;
  DecoderPool* decoders_;
  int16_t frame_[audio::kFrameSamples];
  Counters c_;
};

}  // namespace crewradio
