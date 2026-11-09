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

// The encoder steps its complexity down at run time (encode_budget.h), and
// this libopus is built with its assertions on: a failed one aborts, which
// restarts the unit. So every complexity it can reach, and the step down
// in the middle of a signal, over the signals that trip the encoder's
// assertions at low bitrates (silence, a tone, noise, a square wave, DC).
TEST(opus_encoder_holds_at_every_complexity_the_budget_can_reach) {
  OpusFrameEncoder enc;
  OpusFrameDecoder dec;
  CHECK(enc.ok() && dec.ok());
  int16_t pcm[N], out[N];
  uint8_t payload[FrameEncoder::kMaxPayload];
  uint32_t rng = 12345;
  int bad = 0;
  for (int c = OpusFrameEncoder::kComplexity; c >= 0; c--) {
    enc.set_complexity(c);
    for (int signal = 0; signal < 5; signal++) {
      for (int f = 0; f < 1500; f++) {
        // Step down once more half way through, as the budget does.
        if (f == 750 && c > 0) enc.set_complexity(c - 1);
        if (f == 751 && c > 0) enc.set_complexity(c);
        for (size_t i = 0; i < N; i++) {
          const double t = (double)(f * N + i) / audio::kSampleRate;
          int v = 0;
          switch (signal) {
            case 0: v = 0; break;
            case 1: v = (int)(12000 * sin(2 * M_PI * 440 * t)); break;
            case 2:
              rng = rng * 1103515245u + 12345u;
              v = (int)((rng >> 16) & 0x7FFF) - 16384;
              break;
            case 3: v = ((f * (int)N + (int)i) / 40) % 2 ? 20000 : -20000; break;
            case 4: v = 15000; break;
          }
          pcm[i] = (int16_t)v;
        }
        const int n = enc.encode(pcm, payload);
        if (n <= 0 || dec.decode(payload, (size_t)n, out) != (int)N) bad++;
      }
    }
  }
  CHECK(bad == 0);
}
