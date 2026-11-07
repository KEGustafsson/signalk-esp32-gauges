// SPDX-License-Identifier: EUPL-1.2
#pragma once

// An Opus FrameEncoder: 16 kHz mono, 20 ms, VoIP, 24 kbit/s, as the app's
// encoder. Same libopus build as the decoder, so the same rule: every call
// from the one task that makes all libopus calls (cr-audio).

#include "frame_encoder.h"

struct OpusEncoder;

namespace crewradio {

class OpusFrameEncoder : public FrameEncoder {
 public:
  static constexpr int kBitrate = 24000;
  // Fixed point on a 400 MHz core: a middle setting, measured on the board.
  static constexpr int kComplexity = 5;

  OpusFrameEncoder();
  ~OpusFrameEncoder() override;
  OpusFrameEncoder(const OpusFrameEncoder&) = delete;
  OpusFrameEncoder& operator=(const OpusFrameEncoder&) = delete;

  bool ok() const { return enc_ != nullptr; }
  Codec codec() const override { return Codec::kOpus; }
  int encode(const int16_t frame[audio::kFrameSamples],
             uint8_t out[kMaxPayload]) override;
  void reset() override;

 private:
  ::OpusEncoder* enc_ = nullptr;
};

}  // namespace crewradio
