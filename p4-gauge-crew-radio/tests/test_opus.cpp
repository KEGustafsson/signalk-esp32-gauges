// SPDX-License-Identifier: EUPL-1.2
// The Opus decoder wrapper, over the same libopus the firmware builds
// (CREW_RADIO_OPUS).
#include <math.h>
#include <opus.h>
#include <string.h>

#include <vector>

#include "core/opus_decoder.h"
#include "core/opus_encoder.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr size_t N = audio::kFrameSamples;

// Opus packets of a 440 Hz tone, encoded the way the app does (16 kHz mono,
// 20 ms, VoIP, 24 kbit/s).
std::vector<std::vector<uint8_t>> tone(int frames, int frame_samples = (int)N) {
  int err = 0;
  OpusEncoder* enc =
      opus_encoder_create(audio::kSampleRate, 1, OPUS_APPLICATION_VOIP, &err);
  opus_encoder_ctl(enc, OPUS_SET_BITRATE(24000));
  std::vector<std::vector<uint8_t>> out;
  std::vector<int16_t> pcm((size_t)frame_samples);
  for (int f = 0; f < frames; f++) {
    for (int i = 0; i < frame_samples; i++) {
      const double t = (double)(f * frame_samples + i) / audio::kSampleRate;
      pcm[(size_t)i] = (int16_t)(8000 * sin(2 * M_PI * 440 * t));
    }
    uint8_t buf[400];
    const int n = opus_encode(enc, pcm.data(), frame_samples, buf, sizeof(buf));
    out.emplace_back(buf, buf + (n > 0 ? n : 0));
  }
  opus_encoder_destroy(enc);
  return out;
}

double rms(const int16_t* s) {
  double sum = 0;
  for (size_t i = 0; i < N; i++) sum += (double)s[i] * s[i];
  return sqrt(sum / N);
}

}  // namespace

TEST(opus_decodes_20_ms_frames_at_16_khz) {
  OpusFrameDecoder dec;
  CHECK(dec.ok());
  const auto packets = tone(10);
  int16_t out[N];
  double last = 0;
  for (const auto& p : packets) {
    CHECK(!p.empty());
    CHECK(dec.decode(p.data(), p.size(), out) == (int)N);
    last = rms(out);
  }
  CHECK(last > 2000);  // A tone came through, not silence
  dec.reset();
  CHECK(dec.decode(packets[0].data(), packets[0].size(), out) == (int)N);
}

TEST(opus_refuses_what_is_not_one_20_ms_frame) {
  OpusFrameDecoder dec;
  int16_t out[N];
  CHECK(dec.decode(nullptr, 10, out) == -1);
  const auto p = tone(1);
  CHECK(dec.decode(p[0].data(), 0, out) == -1);
  const auto ten_ms = tone(1, (int)N / 2);
  CHECK(dec.decode(ten_ms[0].data(), ten_ms[0].size(), out) == -1);
}

TEST(opus_encoder_round_trips_through_the_decoder) {
  OpusFrameEncoder enc;
  OpusFrameDecoder dec;
  CHECK(enc.ok() && dec.ok() && enc.codec() == Codec::kOpus);
  int16_t pcm[N], out[N];
  uint8_t payload[FrameEncoder::kMaxPayload];
  double last = 0;
  for (int f = 0; f < 10; f++) {
    for (size_t i = 0; i < N; i++) {
      pcm[i] = (int16_t)(8000 * sin(2 * M_PI * 440 * (double)(f * N + i) /
                                    audio::kSampleRate));
    }
    const int n = enc.encode(pcm, payload);
    CHECK(n > 0 && n < 200);  // 24 kbit/s: about 60 bytes a frame
    CHECK(dec.decode(payload, (size_t)n, out) == (int)N);
    last = rms(out);
  }
  CHECK(last > 2000);
  enc.reset();
  CHECK(enc.encode(pcm, payload) > 0);
}
