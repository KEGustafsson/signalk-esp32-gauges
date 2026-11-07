// SPDX-License-Identifier: EUPL-1.2
// The header, as the app's PacketTest.
#include <string.h>

#include "core/ingress.h"
#include "core/packet.h"
#include "test.h"

using namespace crewradio;

namespace {

std::vector<uint8_t> make(const Header& h, size_t payload = 4) {
  std::vector<uint8_t> p(packet::kHeader + payload, 0xAB);
  packet::encode_header(h, p.data());
  return p;
}

}  // namespace

TEST(packet_round_trips_the_header) {
  Header h;
  h.sender_id = -123456;
  h.seq = 0x7FFFFFFF;
  h.codec = Codec::kOpus;
  h.ttl = 3;
  h.hops = 4;
  h.time = 0xFFFFFFF0u;
  const auto p = make(h);
  Header back;
  CHECK(packet::parse(p.data(), p.size(), &back));
  CHECK(back.sender_id == h.sender_id && back.seq == h.seq);
  CHECK(back.codec == h.codec && back.ttl == 3 && back.hops == 4);
  CHECK(back.time == 0xFFFFFFF0u);  // Unsigned on the wire
}

TEST(packet_rejects_malformed) {
  Header h;
  auto p = make(h);
  Header out;
  CHECK(!packet::parse(p.data(), packet::kHeader, &out));  // No payload
  auto bad = p;
  bad[0] = 'X';
  CHECK(!packet::parse(bad.data(), bad.size(), &out));
  bad = p;
  bad[2] = 3;  // Version 3: no legacy decoding
  CHECK(!packet::parse(bad.data(), bad.size(), &out));
  bad = p;
  bad[3] = 3;  // Unknown codec
  CHECK(!packet::parse(bad.data(), bad.size(), &out));
  const auto big = make(h, packet::kMaxSize);  // Over kMaxSize in total
  CHECK(!packet::parse(big.data(), big.size(), &out));
  const auto max = make(h, packet::kMaxSize - packet::kHeader);
  CHECK(packet::parse(max.data(), max.size(), &out));
  CHECK(!packet::parse(nullptr, 40, &out));
}

TEST(packet_ttl_and_hops_are_clamped_to_a_byte) {
  Header h;
  h.ttl = 300;
  h.hops = -5;
  const auto p = make(h);
  CHECK(p[4] == 255 && p[5] == 0);
  auto q = p;
  packet::set_ttl(q.data(), 2);
  CHECK(q[4] == 2);
  packet::set_ttl(q.data(), -1);
  CHECK(q[4] == 0);
}

TEST(packet_aad_is_the_header_without_the_ttl) {
  Header h;
  h.ttl = 4;
  h.hops = 4;
  h.sender_id = 7;
  auto p = make(h);
  uint8_t a[packet::kHeader], b[packet::kHeader];
  packet::aad_of(p.data(), a);
  packet::set_ttl(p.data(), 1);
  packet::aad_of(p.data(), b);
  CHECK(memcmp(a, b, sizeof(a)) == 0);  // A relay's rewrite does not break it
  CHECK(a[4] == 0 && a[5] == 4);        // hops is authenticated
}

TEST(packet_freshness_is_a_window_either_side_and_wraps) {
  const int64_t now = 1788739200;
  CHECK(packet::is_fresh((uint32_t)now, now));
  CHECK(packet::is_fresh((uint32_t)(now + 60), now));
  CHECK(packet::is_fresh((uint32_t)(now - 60), now));
  CHECK(!packet::is_fresh((uint32_t)(now + 61), now));
  CHECK(!packet::is_fresh((uint32_t)(now - 61), now));
  // 2106: the counter wraps, the distance stays small.
  const int64_t wrap = 0x100000000LL;
  CHECK(packet::is_fresh(5u, wrap - 10));
  CHECK(packet::is_fresh(0xFFFFFFF6u, wrap + 5));
  CHECK(!packet::is_fresh(100u, wrap - 10));
}

TEST(relay_ttl_is_decremented_capped_and_bounded_by_our_hops) {
  Header h;
  h.hops = 4;
  h.ttl = 4;
  CHECK(Ingress::relay_ttl(h, 4) == 3);
  h.ttl = 9;  // Bumped in flight: capped at the signed budget
  CHECK(Ingress::relay_ttl(h, 4) == 3);
  h.ttl = 1;
  CHECK(Ingress::relay_ttl(h, 4) == 0);
  h.ttl = 2;  // Two relays passed; a third would be our 3rd hop of 3
  CHECK(Ingress::relay_ttl(h, 3) == 0);
  CHECK(Ingress::relay_ttl(h, 4) == 1);
  h.ttl = 4;
  CHECK(Ingress::relay_ttl(h, 1) == 0);  // A hop limit of 1 relays nothing
}
