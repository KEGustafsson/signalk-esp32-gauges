// SPDX-License-Identifier: EUPL-1.2
// The decoder pool and the playout, with a fake decoder.
#include <memory>
#include <vector>

#include "core/conceal.h"
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
  CHECK(pool.get(30, 15) == nullptr);  // Both talking: the newcomer waits
  const int64_t later = 5 + DecoderPool::kEvictIdleMs;
  CHECK(pool.get(10, later) == a);  // 10 still talking
  CHECK(pool.get(30, later) == b && b->resets == 2);  // 20 paused 2 s
  pool.sweep(later + DecoderPool::kIdleMs);
  CHECK(pool.in_use() == 2);  // Both heard at `later`: not idle yet
  pool.sweep(later + DecoderPool::kIdleMs + 1);
  CHECK(pool.in_use() == 0);
}

// A ninth Opus talker among eight live ones gets no decoder (its frames are
// dropped) rather than garbling all of them by resetting one every frame.
TEST(pool_a_ninth_talker_never_resets_a_live_decoder) {
  std::vector<std::unique_ptr<FrameDecoder>> d;
  std::vector<FakeDecoder*> raw;
  for (size_t i = 0; i < DecoderPool::kMax; i++) {
    raw.push_back(new FakeDecoder(1));
    d.emplace_back(raw.back());
  }
  DecoderPool pool(std::move(d));
  int refused = 0;
  for (int64_t t = 0; t < 1000; t += 20) {
    for (int32_t s = 1; s <= 9; s++) refused += pool.get(s, t) == nullptr ? 1 : 0;
  }
  int resets = 0;
  for (FakeDecoder* f : raw) resets += f->resets;
  CHECK(resets == (int)DecoderPool::kMax);  // Each taken once, by its talker
  CHECK(refused == 50);                     // The ninth, every frame
}

// A gap longer than concealment covers is not concealed: the burst goes on
// with the frame that came, without three slots of delay before it.
TEST(playout_a_long_gap_is_not_concealed) {
  Mixer mixer;
  DecoderPool pool({});
  Playout play(&mixer, &pool);
  uint8_t pcm[audio::kFrameBytes] = {};
  Delivery dp{1, (uint8_t)Codec::kPcm, 0, 0, (uint16_t)sizeof(pcm), 0};
  for (int i = 0; i < 4; i++) play.deliver(dp, pcm, 0);
  dp.gap = conceal::kMaxFrames + 2;
  play.deliver(dp, pcm, 0);
  int16_t out[N];
  for (int i = 0; i < 5; i++) mixer.tick(i * 20, out);
  CHECK(mixer.concealed_frames() == 0);
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
