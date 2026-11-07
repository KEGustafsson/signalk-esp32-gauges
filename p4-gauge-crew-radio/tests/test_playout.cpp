// SPDX-License-Identifier: EUPL-1.2
// The decoder pool and the playout, with a fake decoder.
#include <memory>
#include <vector>

#include "core/playout.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr size_t N = audio::kFrameSamples;

struct FakeDecoder : FrameDecoder {
  int resets = 0;
  int16_t value;
  explicit FakeDecoder(int16_t v) : value(v) {}
  int decode(const uint8_t*, size_t, int16_t out[N]) override {
    for (size_t i = 0; i < N; i++) out[i] = value;
    return (int)N;
  }
  void reset() override { resets++; }
};

}  // namespace

TEST(pool_gives_each_sender_its_own_and_takes_over_the_quietest) {
  std::vector<std::unique_ptr<FrameDecoder>> d;
  FakeDecoder* a = new FakeDecoder(1);
  FakeDecoder* b = new FakeDecoder(2);
  d.emplace_back(a);
  d.emplace_back(b);
  DecoderPool pool(std::move(d));
  CHECK(pool.get(10, 0) == a && a->resets == 1);
  CHECK(pool.get(20, 5) == b);
  CHECK(pool.get(10, 10) == a && a->resets == 1);  // Same sender: kept
  CHECK(pool.get(30, 15) == b && b->resets == 2);  // 20 was the quietest
  pool.sweep(15 + DecoderPool::kIdleMs);
  CHECK(pool.in_use() == 1);  // 30 heard at 15: not idle yet
  pool.sweep(15 + DecoderPool::kIdleMs + 1);
  CHECK(pool.in_use() == 0);
}

TEST(playout_pcm_and_opus_reach_the_mixer_and_gaps_are_concealed) {
  Mixer mixer;
  std::vector<std::unique_ptr<FrameDecoder>> d;
  d.emplace_back(new FakeDecoder(700));
  DecoderPool pool(std::move(d));
  Playout play(&mixer, &pool);

  uint8_t pcm[audio::kFrameBytes];
  for (size_t i = 0; i < N; i++) {
    pcm[2 * i] = 0x34;  // 0x1234 little endian
    pcm[2 * i + 1] = 0x12;
  }
  Delivery dp{1, (uint8_t)Codec::kPcm, 0, 0, (uint16_t)sizeof(pcm), 0};
  play.deliver(dp, pcm, 0);
  play.deliver(dp, pcm, 0);
  dp.gap = 1;
  play.deliver(dp, pcm, 0);
  Delivery dop{2, (uint8_t)Codec::kOpus, 0, 0, 3, 0};
  const uint8_t junk[3] = {1, 2, 3};
  play.deliver(dop, junk, 0);
  play.deliver(dop, junk, 0);
  Delivery bad{3, (uint8_t)Codec::kPcm, 0, 0, 10, 0};
  play.deliver(bad, pcm, 0);
  CHECK(play.counters().frames == 5 && play.counters().decode_errors == 1);

  int16_t out[N];
  mixer.tick(0, out);
  // 0x1234 + 700, scaled for two talkers: (4660 + 700) x 23170 >> 15.
  CHECK(out[0] == (int16_t)(((int64_t)(0x1234 + 700) * 23170) >> 15));
  mixer.tick(20, out);
  mixer.tick(40, out);  // Sender 1's hole, concealed; sender 2 dry, concealed
  CHECK(mixer.concealed_frames() == 2);
}
