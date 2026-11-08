// SPDX-License-Identifier: EUPL-1.2
// The radio under load (crew-radio's shared rules, 2026-11-08): one
// forwarder per boat, split horizon on Reticulum, and channel busy.
#include "core/channel_floor.h"
#include "core/encode_budget.h"
#include "core/forwarder.h"
#include "test.h"

using namespace crewradio;

namespace {
constexpr uint8_t kUp = Forwarder::kRnsUp;
constexpr uint8_t kPrefer = Forwarder::kFwdPrefer;
constexpr uint8_t kAvoid = Forwarder::kFwdAvoid;
}  // namespace

TEST(forwarder_needs_reticulum_up) {
  Forwarder f;
  int32_t id = 0;
  CHECK(!f.forwarder(7, kAvoid, 0, &id));  // Nobody on Reticulum
  f.local_hello(5, 1, 0);                  // On the WLAN only
  CHECK(!f.forwarder(7, kAvoid, 0, &id));
  CHECK(f.forwarder(7, kAvoid | kUp, 0, &id) && id == 7);
}

TEST(forwarder_rank_then_lowest_unsigned_id) {
  Forwarder f;
  int32_t id = 0;
  f.local_hello(100, 1 | kUp, 0);            // A phone
  CHECK(f.forwarder(50, kAvoid | kUp, 0, &id) && id == 100);  // Phone over ESP
  f.local_hello(900, 1 | kUp | kPrefer, 0);  // The plugin
  CHECK(f.forwarder(50, kAvoid | kUp, 0, &id) && id == 900);
  CHECK(!f.is_forwarder(50, kAvoid | kUp, 0));
  // Ties on rank: the lowest id as an unsigned number, so a negative id
  // (0x80000000 and up) sorts after 1.
  Forwarder g;
  g.local_hello((int32_t)0x80000000u, 1 | kUp, 0);
  g.local_hello(1, 1 | kUp, 0);
  CHECK(g.forwarder(-5, kUp, 0, &id) && id == 1);
  Forwarder h;
  h.local_hello((int32_t)0x80000000u, 1 | kUp, 0);
  CHECK(h.forwarder(-5, kUp, 0, &id) && id == (int32_t)0x80000000u);
}

TEST(forwarder_forgets_quiet_peers) {
  Forwarder f;
  f.local_hello(1, 1 | kUp, 0);
  CHECK(f.is_local(1, Forwarder::kLocalMs - 1));
  CHECK(f.local_rns_up(1, Forwarder::kLocalMs - 1));
  CHECK(!f.is_local(1, Forwarder::kLocalMs));
  CHECK(f.is_forwarder(9, kAvoid | kUp, Forwarder::kLocalMs));  // Alone again
  f.local_hello(1, 1, 20000);  // Back, but off Reticulum now
  CHECK(f.is_local(1, 20000) && !f.local_rns_up(1, 20000));
  CHECK(f.is_forwarder(9, kAvoid | kUp, 20000));
}

TEST(forwarder_audio_routes) {
  using R = Forwarder::Route;
  R r = Forwarder::audio_route(true, false, false);  // Not the forwarder
  CHECK(!r.to_lan && !r.to_reticulum);
  r = Forwarder::audio_route(false, false, false);
  CHECK(!r.to_lan && !r.to_reticulum);
  r = Forwarder::audio_route(true, true, false);  // A WLAN-only talker
  CHECK(r.to_reticulum && !r.to_lan);
  r = Forwarder::audio_route(true, true, true);  // Sends its own: nobody repeats
  CHECK(!r.to_reticulum && !r.to_lan);
  r = Forwarder::audio_route(false, true, true);  // From ashore: onto the WLAN
  CHECK(r.to_lan && !r.to_reticulum);
}

TEST(forwarder_split_horizon_by_link_tag) {
  Forwarder f;
  const uint32_t a = Forwarder::make_tag(3, 1);
  CHECK(Forwarder::tag_slot(a) == 3);
  CHECK(f.skip_tag(3, 0) == 0);  // Far end unknown: it gets the copy
  f.link_heard(a, 42);
  CHECK(f.skip_tag(3, 0) == 0);  // 42 is ashore
  f.local_hello(42, 1 | kUp, 0);
  CHECK(f.skip_tag(3, 0) == a);  // 42 is on the boat: no copy over the hub
  CHECK(f.skip_tag(3, Forwarder::kLocalMs) == 0);  // Gone from the WLAN
  // The slot reused by another link: a new epoch, a new far end.
  const uint32_t b = Forwarder::make_tag(3, 2);
  f.link_heard(b, 77);
  CHECK(f.skip_tag(3, 0) == 0);
  CHECK(f.skip_tag(-1, 0) == 0 && f.skip_tag(Forwarder::kMaxLinks, 0) == 0);
}

TEST(forwarder_full_table_drops_the_oldest) {
  Forwarder f;
  for (int i = 0; i < Forwarder::kMaxLocal; i++) f.local_hello(i + 1, 1, i);
  f.local_hello(1000, 1, 100);
  CHECK(f.is_local(1000, 100));
  CHECK(!f.is_local(1, 100));  // Heard first, made room
  CHECK(f.is_local(2, 100));
}

namespace {
// Samples the floor every 20 ms from t to t_end with the key held as given;
// the last event other than kNone.
ChannelFloor::Event run(ChannelFloor& fl, int64_t& t, int64_t t_end, bool held,
                        bool talking, bool* may) {
  ChannelFloor::Event last = ChannelFloor::Event::kNone;
  for (; t < t_end; t += 20) {
    const ChannelFloor::Event e = fl.sample(held, talking, t, may);
    if (e != ChannelFloor::Event::kNone) last = e;
  }
  return last;
}
}  // namespace

TEST(floor_free_channel_keys) {
  ChannelFloor fl(10);
  bool may = false;
  CHECK(fl.sample(true, false, 0, &may) == ChannelFloor::Event::kNone);
  CHECK(may && !fl.holding_off());
}

TEST(floor_refuses_while_busy_then_override) {
  ChannelFloor fl(10);
  fl.heard(20, 0);
  int32_t who = 0;
  CHECK(fl.busy(100, &who) && who == 20);
  bool may = true;
  CHECK(fl.sample(true, false, 100, &may) == ChannelFloor::Event::kRefused);
  CHECK(!may && fl.holding_off());
  int64_t t = 120;
  // The other talker goes on; we keep holding: still refused...
  CHECK(run(fl, t, 100 + ChannelFloor::kOverrideMs, true, false, &may) ==
        ChannelFloor::Event::kNone);
  CHECK(!may);
  // ...until kOverrideMs after the cue: keys anyway.
  CHECK(fl.sample(true, false, t, &may) == ChannelFloor::Event::kOverride);
  CHECK(may && !fl.holding_off());
  // An override is never made to yield.
  fl.heard(1, t);
  CHECK(fl.sample(true, true, t + 20, &may) == ChannelFloor::Event::kNone);
  CHECK(may);
}

TEST(floor_release_before_override_resets) {
  ChannelFloor fl(10);
  fl.heard(20, 0);
  bool may = true;
  CHECK(fl.sample(true, false, 0, &may) == ChannelFloor::Event::kRefused);
  int64_t t = 20;
  run(fl, t, 100, false, false, &may);  // Let go (60 ms and more)
  CHECK(may && !fl.holding_off());
  // The channel quiet now (the talker last heard at 0): keys at once.
  CHECK(fl.sample(true, false, 1000, &may) == ChannelFloor::Event::kNone);
  CHECK(may);
}

TEST(floor_short_touch_gap_keeps_refusal) {
  ChannelFloor fl(10);
  fl.heard(20, 0);
  bool may = true;
  fl.sample(true, false, 0, &may);
  fl.sample(false, false, 20, &may);  // One missed touch read
  fl.sample(false, false, 40, &may);
  CHECK(!may && fl.holding_off());   // Not a release yet
}

TEST(floor_collision_higher_id_yields) {
  // Both keyed at once; each hears the other's first frames while talking.
  ChannelFloor low(5), high(9);
  bool may_low = false, may_high = false;
  low.sample(true, false, 0, &may_low);
  high.sample(true, false, 0, &may_high);
  low.heard(9, 40);
  high.heard(5, 40);
  CHECK(low.sample(true, true, 40, &may_low) == ChannelFloor::Event::kNone);
  CHECK(may_low);
  CHECK(high.sample(true, true, 40, &may_high) == ChannelFloor::Event::kYielded);
  CHECK(!may_high && high.holding_off());
  // The yielded one stays off while held, whatever it hears or not.
  int64_t t = 60;
  CHECK(run(high, t, 3000, true, false, &may_high) == ChannelFloor::Event::kNone);
  CHECK(!may_high);
  // Let go and pressed again on a quiet channel: talks.
  run(high, t, t + 100, false, false, &may_high);
  CHECK(high.sample(true, false, t + 1000, &may_high) ==
        ChannelFloor::Event::kNone);
  CHECK(may_high);
}

TEST(floor_collision_compares_unsigned) {
  // 0x80000000 is above 7 as unsigned: it yields to 7, not the other way.
  ChannelFloor neg((int32_t)0x80000000u), pos(7);
  bool m1 = false, m2 = false;
  neg.sample(true, false, 0, &m1);
  pos.sample(true, false, 0, &m2);
  neg.heard(7, 20);
  pos.heard((int32_t)0x80000000u, 20);
  CHECK(neg.sample(true, true, 20, &m1) == ChannelFloor::Event::kYielded);
  CHECK(pos.sample(true, true, 20, &m2) == ChannelFloor::Event::kNone);
  CHECK(!m1 && m2);
}

TEST(floor_ignores_own_frames_and_old_talkers) {
  ChannelFloor fl(10);
  fl.heard(10, 0);  // Our own, relayed back
  CHECK(!fl.busy(0));
  fl.heard(3, 0);
  CHECK(fl.busy(ChannelFloor::kBusyMs - 1));
  CHECK(!fl.busy(ChannelFloor::kBusyMs));
  bool may = false;
  // A talker heard only before we keyed, and gone: no collision later.
  CHECK(fl.sample(true, false, 1000, &may) == ChannelFloor::Event::kNone);
  CHECK(fl.sample(true, true, 1020, &may) == ChannelFloor::Event::kNone);
  CHECK(may);
}

// ---- the encoder's budget (encode_budget.h) -------------------------------

TEST(encode_budget_steps_down_on_a_slow_window_only) {
  EncodeBudget b(5);
  for (int i = 0; i < EncodeBudget::kWindow; i++) CHECK(!b.took(4000));
  CHECK(b.complexity() == 5);  // Fast enough
  bool changed = false;
  for (int i = 0; i < EncodeBudget::kWindow; i++) {
    changed = b.took(i == 3 ? EncodeBudget::kHighUs + 1 : 4000) || changed;
  }
  CHECK(changed && b.complexity() == 4);  // One slow frame in the window
  // Exactly at the limit is not over it.
  for (int i = 0; i < EncodeBudget::kWindow; i++) b.took(EncodeBudget::kHighUs);
  CHECK(b.complexity() == 4);
}

TEST(encode_budget_stops_at_zero_and_never_steps_up) {
  EncodeBudget b(2);
  for (int w = 0; w < 5; w++) {
    for (int i = 0; i < EncodeBudget::kWindow; i++) b.took(25000);
  }
  CHECK(b.complexity() == 0);
  for (int i = 0; i < EncodeBudget::kWindow; i++) CHECK(!b.took(25000));
  for (int w = 0; w < 5; w++) {
    for (int i = 0; i < EncodeBudget::kWindow; i++) b.took(100);
  }
  CHECK(b.complexity() == 0);
}
