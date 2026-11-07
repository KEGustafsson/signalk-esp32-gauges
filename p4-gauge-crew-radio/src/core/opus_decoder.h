// SPDX-License-Identifier: EUPL-1.2
#pragma once

// An Opus FrameDecoder over libopus (pschatzmann/codec-opus on the board and
// in the host tests: the same sources), mono, decoding at 16 kHz directly --
// libopus decodes at any of its rates, so the app's 48 kHz Decimator is not
// needed here.
//
// That build of libopus keeps its scratch memory in one global buffer
// (NONTHREADSAFE_PSEUDOSTACK): every libopus call must come from one task.
// The playout task makes them all; phase 2's encoder must run there too.

#include <stddef.h>
#include <stdint.h>

#include "decoder_pool.h"

struct OpusDecoder;

namespace crewradio {

class OpusFrameDecoder : public FrameDecoder {
 public:
  OpusFrameDecoder();
  ~OpusFrameDecoder() override;
  OpusFrameDecoder(const OpusFrameDecoder&) = delete;
  OpusFrameDecoder& operator=(const OpusFrameDecoder&) = delete;

  bool ok() const { return dec_ != nullptr; }
  int decode(const uint8_t* data, size_t len,
             int16_t out[audio::kFrameSamples]) override;
  void reset() override;

 private:
  ::OpusDecoder* dec_ = nullptr;
};

}  // namespace crewradio
