// SPDX-License-Identifier: EUPL-1.2
#include "playout.h"

#include "conceal.h"

#include <string.h>

namespace crewradio {

void Playout::deliver(const Delivery& d, const uint8_t* payload,
                      int64_t now_ms) {
  // A gap of up to conceal::kMaxFrames is concealed, as the app's engine
  // does; a longer one is not (the burst simply goes on with this frame):
  // reserving three slots for it would only delay the rest of the burst.
  if (d.gap > 0 && d.gap <= conceal::kMaxFrames) {
    mixer_->conceal(d.sender, d.gap);
  }
  if (d.codec == (uint8_t)Codec::kPcm) {
    if (d.len != audio::kFrameBytes) {
      c_.decode_errors++;
      return;
    }
    // PCM16LE on the wire; both the P4 and the hosts we test on are little
    // endian, but say it rather than assume it.
    for (size_t i = 0; i < audio::kFrameSamples; i++) {
      frame_[i] = (int16_t)((uint16_t)payload[2 * i] |
                            ((uint16_t)payload[2 * i + 1] << 8));
    }
  } else if (d.codec == (uint8_t)Codec::kOpus) {
    FrameDecoder* dec = decoders_->get(d.sender, now_ms);
    if (dec == nullptr || dec->decode(payload, d.len, frame_) < 0) {
      c_.decode_errors++;
      return;
    }
  } else {
    c_.decode_errors++;
    return;
  }
  if (mixer_->push(d.sender, frame_, now_ms)) {
    c_.frames++;
  } else {
    c_.refused++;
  }
}

}  // namespace crewradio
