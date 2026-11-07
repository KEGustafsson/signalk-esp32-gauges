// SPDX-License-Identifier: EUPL-1.2
// The talk side: the talk key and its time-out, the transmitter (its packets
// open on another node as this node's voice), the burst floor, the peer
// snapshot, the key tones and the mic helpers.
#include <string.h>

#include <thread>

#include "core/frame_encoder.h"
#include "core/mic.h"
#include "core/net_node.h"
#include "core/peer_snapshot.h"
#include "core/talk_key.h"
#include "core/tones.h"
#include "core/transmitter.h"
#include "test.h"

using namespace crewradio;

namespace {
constexpr int64_t kNow = 1788739200;
constexpr size_t N = audio::kFrameSamples;
const uint8_t kKey[32] = {3};
}  // namespace

TEST(talk_key_debounces_press_and_release) {
  TalkKey k;
  CHECK(k.sample(true, false, true, 0) == TalkKey::Event::kNone);  // One sample: a bounce
  CHECK(k.sample(false, false, true, 20) == TalkKey::Event::kNone);
  CHECK(k.sample(true, false, true, 40) == TalkKey::Event::kNone);
  CHECK(k.sample(true, false, true, 60) == TalkKey::Event::kKeyed);
  CHECK(k.talking());
  CHECK(k.sample(false, false, true, 80) == TalkKey::Event::kNone);  // A missed read
  CHECK(k.sample(false, false, true, 100) == TalkKey::Event::kNone);
  CHECK(k.sample(false, true, true, 120) == TalkKey::Event::kNone);  // Touch holds it
  CHECK(k.talking());
  k.sample(false, false, true, 140);
  k.sample(false, false, true, 160);
  CHECK(k.sample(false, false, true, 180) == TalkKey::Event::kUnkeyed);
  CHECK(!k.talking());
}

TEST(talk_key_times_out_and_stays_off_until_released) {
  TalkKey k;
  k.sample(true, false, true, 0);
  k.sample(true, false, true, 20);
  CHECK(k.talking());
  CHECK(k.sample(true, false, true, 20 + TalkKey::kTimeoutMs - 1) == TalkKey::Event::kNone);
  CHECK(k.sample(true, false, true, 20 + TalkKey::kTimeoutMs) == TalkKey::Event::kTimedOut);
  CHECK(!k.talking() && k.locked_out());
  // Still held (a shorted button): never keys again by itself.
  for (int i = 0; i < 100; i++) {
    CHECK(k.sample(true, true, true, 200000 + i * 20) == TalkKey::Event::kNone);
  }
  k.sample(false, false, true, 300000);
  k.sample(false, false, true, 300020);
  k.sample(false, false, true, 300040);
  CHECK(!k.locked_out());
  k.sample(false, true, true, 300060);
  CHECK(k.sample(false, true, true, 300080) == TalkKey::Event::kKeyed);
}

TEST(transmitter_packets_open_as_this_nodes_voice_in_sequence) {
  Transmitter tx(kKey, 77, 4);
  PcmFrameEncoder pcm;
  int16_t frame[N];
  for (size_t i = 0; i < N; i++) frame[i] = (int16_t)(i * 50 - 8000);
  NetNode rx(kKey, 1, NetNode::Config());
  uint8_t pkt[Transmitter::kMaxPacket];
  for (int i = 0; i < 3; i++) {
    const size_t n = tx.build(&pcm, frame, kNow, pkt);
    CHECK(n == packet::kHeader + audio::kFrameBytes + ChannelCrypto::kOverhead);
    Header h;
    CHECK(packet::parse(pkt, n, &h) && h.seq == i && h.codec == Codec::kPcm);
    CHECK(h.ttl == 4 && h.hops == 4 && h.time == (uint32_t)kNow);
    NetNode::Audio au;
    CHECK(rx.on_datagram(pkt, n, 9, i * 20, kNow, &au));
    CHECK(au.sender == 77 && au.gap == 0 && au.len == audio::kFrameBytes);
    CHECK(au.data[0] == (uint8_t)((uint16_t)frame[0] & 0xFF));
    CHECK(au.data[1] == (uint8_t)((uint16_t)frame[0] >> 8));
  }
  CHECK(tx.sent() == 3);
  CHECK(tx.build(nullptr, frame, kNow, pkt) == 0);
}

TEST(burst_floor_first_frames_of_each_burst) {
  BurstFloor f;
  int kept = 0;
  for (int i = 0; i < 20; i++) kept += f.take(7, i * 20) ? 1 : 0;
  CHECK(kept == BurstFloor::kFrames);
  // The last frame was at 380 ms: 400 ms later is not yet a new burst...
  CHECK(!f.take(7, 380 + BurstFloor::kGapMs));
  // ...and more than 400 ms after that one is.
  CHECK(f.take(7, 380 + 2 * BurstFloor::kGapMs + 1));
}

TEST(peer_snapshot_reads_what_was_published_and_never_a_torn_copy) {
  PeerSnapshot snap;
  PeerView v;
  CHECK(snap.read(&v) && v.ip == 0 && v.count == 0);
  PeerView w;
  w.ip = 0x0A000005;
  w.broadcast = 0x0A0000FF;
  w.count = 2;
  w.addrs[0] = 1;
  w.addrs[1] = 2;
  snap.publish(w);
  CHECK(snap.read(&v) && v.ip == w.ip && v.count == 2 && v.addrs[1] == 2);
  // A writer publishing views whose every field is the same number; a
  // reader must never see two numbers mixed. Start from such a view too.
  PeerView zero;
  zero.count = LanPeers::kMax;
  snap.publish(zero);
  std::thread writer([&] {
    for (uint32_t k = 1; k < 200000; k++) {
      PeerView x;
      x.ip = k;
      x.broadcast = k;
      x.count = LanPeers::kMax;
      for (int i = 0; i < LanPeers::kMax; i++) x.addrs[i] = k;
      snap.publish(x);
    }
  });
  bool torn = false;
  for (int r = 0; r < 200000; r++) {
    PeerView x;
    if (!snap.read(&x)) continue;
    for (int i = 0; i < x.count; i++) torn = torn || x.addrs[i] != x.ip;
    torn = torn || x.broadcast != x.ip;
  }
  writer.join();
  CHECK(!torn);
}

TEST(tones_are_whole_frames_and_fade) {
  for (auto cue : {tones::Cue::kKeyed, tones::Cue::kUnkeyed, tones::Cue::kTimedOut}) {
    const auto f = tones::frames(cue);
    CHECK(!f.empty() && f.size() % N == 0);
    CHECK(f.front() == 0);  // Fades in: no click
    int peak = 0;
    for (int16_t s : f) peak = s > peak ? s : peak;
    CHECK(peak > 5000 && peak <= 6000);
  }
  CHECK(tones::frames(tones::Cue::kTimedOut).size() >
        tones::frames(tones::Cue::kKeyed).size());
}

TEST(mic_channel_level_and_gain_code) {
  int16_t stereo[2 * N], mono[N];
  for (size_t i = 0; i < N; i++) {
    stereo[2 * i] = 100;
    stereo[2 * i + 1] = -300;
  }
  mic::take_channel(stereo, 0, mono);
  CHECK(mono[0] == 100 && mono[N - 1] == 100 && mic::rms(mono) == 100);
  mic::take_channel(stereo, 1, mono);
  CHECK(mono[5] == -300 && mic::rms(mono) == 300);
  CHECK(mic::es7210_gain_code(0) == 0);
  CHECK(mic::es7210_gain_code(3) == 1);
  CHECK(mic::es7210_gain_code(30) == 10);
  CHECK(mic::es7210_gain_code(33) == 10);  // As esp_codec_dev rounds it
  CHECK(mic::es7210_gain_code(34.5f) == 12);
  CHECK(mic::es7210_gain_code(36) == 13);
  CHECK(mic::es7210_gain_code(37.5f) == 14);
  CHECK(mic::es7210_gain_code(99) == 14);
  CHECK(mic::es7210_gain_code(-5) == 0);
}

// A moment when talking is not allowed (the memory guard, the clock) un-keys
// like a release, but a stuck input stays locked out: only letting go ends
// the time-out.
TEST(talk_a_lock_out_survives_a_moment_when_talking_is_not_allowed) {
  TalkKey k;
  int64_t t = 0;
  k.sample(true, false, true, t);
  CHECK(k.sample(true, false, true, t += 20) == TalkKey::Event::kKeyed);
  CHECK(k.sample(true, false, true, t += TalkKey::kTimeoutMs) ==
        TalkKey::Event::kTimedOut);
  for (int i = 0; i < 10; i++) k.sample(true, false, false, t += 20);  // Not allowed
  CHECK(k.locked_out());
  for (int i = 0; i < 100; i++) {
    CHECK(k.sample(true, false, true, t += 20) == TalkKey::Event::kNone);
  }
  CHECK(k.locked_out());
  for (int i = 0; i < 3; i++) k.sample(false, false, true, t += 20);
  CHECK(!k.locked_out());
}

// Talking not allowed while keyed un-keys after the release frames, and a
// press while it is not allowed does not key.
TEST(talk_not_allowed_unkeys_and_does_not_key) {
  TalkKey k;
  k.sample(true, false, true, 0);
  CHECK(k.sample(true, false, true, 20) == TalkKey::Event::kKeyed);
  k.sample(true, false, false, 40);
  k.sample(true, false, false, 60);
  CHECK(k.sample(true, false, false, 80) == TalkKey::Event::kUnkeyed);
  for (int i = 0; i < 10; i++) {
    CHECK(k.sample(true, true, false, 100 + i * 20) == TalkKey::Event::kNone);
  }
  CHECK(!k.talking());
}

// A stuck input with a moment every minute when talking is not allowed: it
// keys again after each, but the 90 s run from its first keying, so it still
// times out (each moment used to restart them, and it held the channel for
// good).
TEST(talk_a_stuck_input_times_out_through_moments_when_talking_is_not_allowed) {
  TalkKey k;
  int timeouts = 0;
  int64_t t = 0;
  for (; t < 10 * 60000 && timeouts == 0; t += 20) {
    const bool allowed = (t % 60000) >= 100;  // 100 ms not allowed a minute
    if (k.sample(true, false, allowed, t) == TalkKey::Event::kTimedOut) timeouts++;
  }
  CHECK(timeouts == 1);
  CHECK(t <= 20 + TalkKey::kTimeoutMs + 60000);  // Within a minute of the 90 s
  CHECK(k.locked_out() && !k.talking());
  // A real release and a new press: a new 90 s.
  for (int i = 0; i < 3; i++) k.sample(false, false, true, t += 20);
  k.sample(true, false, true, t += 20);
  CHECK(k.sample(true, false, true, t += 20) == TalkKey::Event::kKeyed);
  CHECK(k.sample(true, false, true, t += TalkKey::kTimeoutMs - 100) ==
        TalkKey::Event::kNone);
}
