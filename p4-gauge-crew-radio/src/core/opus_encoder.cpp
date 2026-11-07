// SPDX-License-Identifier: EUPL-1.2
#include "opus_encoder.h"

#include <opus.h>

namespace crewradio {

OpusFrameEncoder::OpusFrameEncoder() {
  int err = OPUS_OK;
  enc_ = opus_encoder_create(audio::kSampleRate, 1, OPUS_APPLICATION_VOIP,
                             &err);
  if (err != OPUS_OK) {
    enc_ = nullptr;
    return;
  }
  opus_encoder_ctl(enc_, OPUS_SET_BITRATE(kBitrate));
  opus_encoder_ctl(enc_, OPUS_SET_COMPLEXITY(kComplexity));
  opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
  // Constant bitrate, as the app asks of MediaCodec (BITRATE_MODE_CBR): the
  // same size every frame, so a burst never spikes the WLAN or a link.
  opus_encoder_ctl(enc_, OPUS_SET_VBR(0));
}

OpusFrameEncoder::~OpusFrameEncoder() {
  if (enc_ != nullptr) opus_encoder_destroy(enc_);
}

int OpusFrameEncoder::encode(const int16_t frame[audio::kFrameSamples],
                             uint8_t out[kMaxPayload]) {
  if (enc_ == nullptr) return -1;
  const int n = opus_encode(enc_, frame, (int)audio::kFrameSamples, out,
                            (opus_int32)kMaxPayload);
  return n > 0 ? n : -1;
}

void OpusFrameEncoder::reset() {
  if (enc_ != nullptr) opus_encoder_ctl(enc_, OPUS_RESET_STATE);
}

}  // namespace crewradio
