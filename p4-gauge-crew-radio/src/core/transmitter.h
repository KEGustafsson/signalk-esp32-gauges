// SPDX-License-Identifier: EUPL-1.2
#pragma once

// This node's own audio packets: a 20 ms frame encoded, numbered in the
// audio sequence (hellos count in their own), stamped with the wall clock
// and the hop budget, and sealed under the packet key with the header as
// AAD. The same sender id as the hellos (NetNode::self_id()), so the phones
// put the voice on the roster entry the hellos made. Owned by the audio
// task, with its own ChannelCrypto (the receive task has another).

#include <stddef.h>
#include <stdint.h>

#include "channel_crypto.h"
#include "frame_encoder.h"
#include "packet.h"

namespace crewradio {

class Transmitter {
 public:
  static constexpr size_t kMaxPacket =
      packet::kHeader + FrameEncoder::kMaxPayload + ChannelCrypto::kOverhead;

  Transmitter(const uint8_t key[ChannelCrypto::kKeyBytes], int32_t self_id,
              int max_hops)
      : crypto_(key), self_(self_id), hops_(max_hops) {}

  bool ok() const { return crypto_.ok(); }

  // The sealed packet for `frame`, its length, or 0.
  size_t build(FrameEncoder* encoder, const int16_t frame[audio::kFrameSamples],
               int64_t now_s, uint8_t out[kMaxPacket]);

  uint64_t sent() const { return sent_; }

 private:
  ChannelCrypto crypto_;
  int32_t self_;
  int hops_;
  int32_t seq_ = 0;
  uint64_t sent_ = 0;
  uint8_t payload_[FrameEncoder::kMaxPayload];
};

}  // namespace crewradio
