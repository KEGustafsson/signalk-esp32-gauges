// SPDX-License-Identifier: EUPL-1.2
#pragma once

// One 20 ms frame of 16 kHz mono PCM into one packet payload: PCM16LE (the
// wire's codec 0, 640 bytes) or Opus (codec 1, opus_encoder.h). The phones
// decode per packet, so a node may send either.

#include <stddef.h>
#include <stdint.h>

#include "audio_config.h"
#include "packet.h"

namespace crewradio {

class FrameEncoder {
 public:
  static constexpr size_t kMaxPayload = audio::kFrameBytes;

  virtual ~FrameEncoder() = default;
  virtual Codec codec() const = 0;
  // The payload length written to `out` (at most kMaxPayload), or -1.
  virtual int encode(const int16_t frame[audio::kFrameSamples],
                     uint8_t out[kMaxPayload]) = 0;
  // A new talk burst: no state carried over from the last one.
  virtual void reset() {}
};

class PcmFrameEncoder : public FrameEncoder {
 public:
  Codec codec() const override { return Codec::kPcm; }
  int encode(const int16_t frame[audio::kFrameSamples],
             uint8_t out[kMaxPayload]) override {
    for (size_t i = 0; i < audio::kFrameSamples; i++) {
      out[2 * i] = (uint8_t)((uint16_t)frame[i] & 0xFF);
      out[2 * i + 1] = (uint8_t)((uint16_t)frame[i] >> 8);
    }
    return (int)audio::kFrameBytes;
  }
};

}  // namespace crewradio
