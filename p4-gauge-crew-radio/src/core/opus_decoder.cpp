// SPDX-License-Identifier: EUPL-1.2
#include "opus_decoder.h"

#include <opus.h>

namespace crewradio {

OpusFrameDecoder::OpusFrameDecoder() {
  int err = OPUS_OK;
  dec_ = opus_decoder_create(audio::kSampleRate, 1, &err);
  if (err != OPUS_OK) dec_ = nullptr;
}

OpusFrameDecoder::~OpusFrameDecoder() {
  if (dec_ != nullptr) opus_decoder_destroy(dec_);
}

int OpusFrameDecoder::decode(const uint8_t* data, size_t len,
                             int16_t out[audio::kFrameSamples]) {
  if (dec_ == nullptr || data == nullptr || len == 0 || len > 1275) return -1;
  // 1275 bytes is Opus's largest frame (RFC 6716); the wire's 1024-byte
  // packets never carry more anyway.
  const int n = opus_decode(dec_, data, (opus_int32)len, out,
                            (int)audio::kFrameSamples, 0);
  // Exactly one 20 ms frame: anything else is not what the crew sends.
  return n == (int)audio::kFrameSamples ? n : -1;
}

void OpusFrameDecoder::reset() {
  if (dec_ != nullptr) opus_decoder_ctl(dec_, OPUS_RESET_STATE);
}

}  // namespace crewradio
