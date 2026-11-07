// SPDX-License-Identifier: EUPL-1.2
// The receive pipeline's order, as the app's IngressTest.
#include "core/ingress.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr int64_t kNow = 1788739200;

Header header(int32_t sender, int32_t seq, Codec codec = Codec::kOpus,
              int ttl = 4) {
  Header h;
  h.sender_id = sender;
  h.seq = seq;
  h.codec = codec;
  h.ttl = ttl;
  h.hops = 4;
  h.time = (uint32_t)kNow;
  return h;
}

int opens = 0;
int good() {
  opens++;
  return 10;
}
int bad() {
  opens++;
  return -1;
}

Ingress::Result admit(Ingress& in, const Header& h, int64_t ms = 0,
                      int (*open)() = good) {
  return in.admit(h, ms, kNow, 4, open);
}

RateLimiter::Config tiny_global() {
  RateLimiter::Config c;
  c.global_burst = 1;
  c.global_per_second = 1;
  return c;
}

}  // namespace

TEST(ingress_a_new_packet_is_accepted_once) {
  Ingress in;
  const auto r = admit(in, header(1, 1));
  CHECK(r.kind == Ingress::Kind::kAccept && r.plain_len == 10 && r.relay_ttl == 3);
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kDuplicate);
}

TEST(ingress_hellos_and_audio_number_themselves_independently) {
  Ingress in;
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kAccept);
  CHECK(admit(in, header(1, 1, Codec::kHello)).kind == Ingress::Kind::kAccept);
}

TEST(ingress_copies_are_not_charged_to_the_sender) {
  RateLimiter::Config c;
  c.burst = 2;
  c.per_second = 1;
  Ingress in(c, 64, 16);
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kAccept);
  for (int i = 0; i < 10; i++) {
    CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kDuplicate);
  }
  CHECK(admit(in, header(1, 2)).kind == Ingress::Kind::kAccept);  // Budget left
  const auto over = admit(in, header(1, 3));
  CHECK(over.kind == Ingress::Kind::kRejected &&
        over.why == Ingress::Why::kSenderBudget);
  // Over budget wrote nothing into the cache: with a token back a second
  // later the same packet is new, not a duplicate.
  CHECK(admit(in, header(1, 3), 1000).kind == Ingress::Kind::kAccept);
}

TEST(ingress_the_global_budget_is_charged_before_opening) {
  Ingress in(tiny_global(), 64, 16);
  opens = 0;
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kAccept);
  const auto r = admit(in, header(1, 2));
  CHECK(r.kind == Ingress::Kind::kRejected && r.why == Ingress::Why::kGlobalBudget);
  CHECK(opens == 1);  // The second was never opened
}

TEST(ingress_a_forgery_never_enters_the_cache_nor_charges_the_sender) {
  Ingress in;
  const auto r = admit(in, header(1, 1), 0, bad);
  CHECK(r.kind == Ingress::Kind::kRejected && r.why == Ingress::Why::kUnreadable);
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kAccept);
}

TEST(ingress_a_flood_of_unreadable_packets_is_noticed) {
  Ingress in;
  Ingress::Why last = Ingress::Why::kNone;
  for (int i = 0; i < 401; i++) last = admit(in, header(1, i), 0, bad).why;
  CHECK(last == Ingress::Why::kJunkFlood);
}

TEST(ingress_stale_is_after_the_tag_and_before_the_cache) {
  Ingress in;
  Header old = header(1, 1);
  old.time = (uint32_t)(kNow - 61);
  CHECK(admit(in, old).kind == Ingress::Kind::kStale);
  CHECK(admit(in, old, 0, bad).why == Ingress::Why::kUnreadable);  // Tag first
  CHECK(admit(in, header(1, 1)).kind == Ingress::Kind::kAccept);   // No cache
  Header wrap = header(2, 1);
  wrap.time = 5;
  CHECK(in.admit(wrap, 0, 0x100000000LL - 10, 4, good).kind ==
        Ingress::Kind::kAccept);
}

TEST(ingress_a_copy_with_a_lowered_ttl_does_not_stop_the_genuine_one) {
  Ingress in;
  const auto first = admit(in, header(1, 1, Codec::kOpus, 1));  // Lowered, first
  CHECK(first.kind == Ingress::Kind::kAccept && first.relay_ttl == 0);
  const auto genuine = admit(in, header(1, 1, Codec::kOpus, 4));
  CHECK(genuine.kind == Ingress::Kind::kRelayOnly && genuine.relay_ttl == 3);
  // No further than that: a plain duplicate.
  CHECK(admit(in, header(1, 1, Codec::kOpus, 4)).kind == Ingress::Kind::kDuplicate);
  CHECK(admit(in, header(1, 1, Codec::kOpus, 9)).kind == Ingress::Kind::kDuplicate);
}

TEST(ingress_our_own_frame_is_a_duplicate_that_still_costs_the_global_budget) {
  Ingress in(tiny_global(), 64, 16);
  opens = 0;
  CHECK(in.admit(header(7, 1), 0, kNow, 4, good, true, 7).kind ==
        Ingress::Kind::kDuplicate);
  CHECK(opens == 0);
  CHECK(in.admit(header(7, 2), 0, kNow, 4, good, true, 7).why ==
        Ingress::Why::kGlobalBudget);
  CHECK(in.admit(header(7, 3), 2000, kNow, 4, good).kind ==
        Ingress::Kind::kAccept);  // Without a self id, a stranger
}

TEST(ingress_audio_gaps_and_hello_gaps_are_counted_apart) {
  Ingress in;
  CHECK(in.admit_audio(1, 10) == 0);
  CHECK(in.admit_audio(1, 13) == 2);
  CHECK(in.admit_audio(1, 12) == -1);
  CHECK(in.hello_gap(1, 1) == 0);
  CHECK(in.hello_gap(1, 3) == 1);
}
