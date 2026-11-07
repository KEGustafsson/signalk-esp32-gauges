// SPDX-License-Identifier: EUPL-1.2
#include "transmitter.h"

namespace crewradio {

size_t Transmitter::build(FrameEncoder* encoder,
                          const int16_t frame[audio::kFrameSamples],
                          int64_t now_s, uint8_t out[kMaxPacket]) {
  if (encoder == nullptr) return 0;
  const int n = encoder->encode(frame, payload_);
  if (n <= 0) return 0;
  Header h;
  h.sender_id = self_;
  // The sequence moves even past a frame that fails to seal: the receivers
  // then count it as lost and conceal it, rather than see a gap close.
  h.seq = (int32_t)seq_++;
  h.codec = encoder->codec();
  h.ttl = hops_;
  h.hops = hops_;
  h.time = (uint32_t)now_s;
  packet::encode_header(h, out);
  uint8_t aad[packet::kHeader];
  packet::aad_of(out, aad);
  if (!crypto_.seal(aad, sizeof(aad), payload_, (size_t)n,
                    out + packet::kHeader)) {
    return 0;
  }
  sent_++;
  return packet::kHeader + (size_t)n + ChannelCrypto::kOverhead;
}

}  // namespace crewradio
