// SPDX-License-Identifier: EUPL-1.2
// The network task's pipeline end to end: two nodes with one key talk; a
// stranger, a copy and a replay are refused; peers are learned only from
// packets that opened.
#include <string.h>

#include "core/net_node.h"
#include "test.h"

using namespace crewradio;

namespace {

constexpr int64_t kNow = 1788739200;
const uint8_t kKey[32] = {7};

NetNode::Config config(const char* name) {
  NetNode::Config c;
  c.name = name;
  return c;
}

// An audio packet from `sender`, sealed under `key`.
std::vector<uint8_t> sealed_audio(const uint8_t* key, int32_t sender, int32_t seq,
                           Codec codec = Codec::kOpus, size_t len = 40,
                           int64_t time = kNow) {
  Header h;
  h.sender_id = sender;
  h.seq = seq;
  h.codec = codec;
  h.ttl = 4;
  h.hops = 4;
  h.time = (uint32_t)time;
  std::vector<uint8_t> plain(len, 0x55);
  std::vector<uint8_t> p(packet::kHeader + len + ChannelCrypto::kOverhead);
  packet::encode_header(h, p.data());
  uint8_t aad[packet::kHeader];
  packet::aad_of(p.data(), aad);
  ChannelCrypto c(key);
  c.seal(aad, sizeof(aad), plain.data(), len, p.data() + packet::kHeader);
  return p;
}

}  // namespace

TEST(net_a_hello_from_one_node_puts_it_on_the_others_roster) {
  NetNode a(kKey, 1, config("Station"));
  NetNode b(kKey, 2, config("Phone"));
  uint8_t buf[NetNode::kMaxHello];
  const size_t n = a.build_hello(kNow, buf);
  CHECK(n == packet::kHeader + Hello::kHead + 7 + ChannelCrypto::kOverhead);
  NetNode::Audio au;
  CHECK(!b.on_datagram(buf, n, 0x0A000001, 0, kNow, &au));
  std::vector<Member> m;
  b.roster().snapshot(0, &m);
  CHECK(m.size() == 1 && m[0].name == "Station" && m[0].id == 1);
  CHECK(m[0].hops == 0 && m[0].transports == Hello::kLan);
  CHECK(b.peers().live(0) == 1);  // Learned: it opened
  // Hello numbers count up: the next is not a duplicate.
  const size_t n2 = a.build_hello(kNow, buf);
  b.on_datagram(buf, n2, 0x0A000001, 1000, kNow, &au);
  CHECK(b.counters().hellos == 2 && b.counters().duplicates == 0);
}

TEST(net_audio_is_handed_on_once_with_its_gap) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  auto p = sealed_audio(kKey, 5, 10);
  CHECK(b.on_datagram(p.data(), p.size(), 1, 0, kNow, &au));
  CHECK(au.sender == 5 && au.codec == Codec::kOpus && au.gap == 0 && au.len == 40);
  CHECK(au.data[0] == 0x55);
  CHECK(!b.on_datagram(p.data(), p.size(), 2, 0, kNow, &au));  // The copy
  p = sealed_audio(kKey, 5, 13);
  CHECK(b.on_datagram(p.data(), p.size(), 1, 20, kNow, &au) && au.gap == 2);
  p = sealed_audio(kKey, 5, 12);  // Late
  CHECK(!b.on_datagram(p.data(), p.size(), 1, 40, kNow, &au));
  CHECK(b.counters().late == 1 && b.counters().duplicates == 1);
  std::vector<Member> m;
  b.roster().snapshot(40, &m);
  CHECK(m.size() == 1 && m[0].talking);
}

TEST(net_a_stranger_teaches_nothing_and_a_stale_packet_plays_nothing) {
  NetNode b(kKey, 2, config("Phone"));
  const uint8_t other[32] = {8};
  NetNode::Audio au;
  auto p = sealed_audio(other, 5, 1);
  CHECK(!b.on_datagram(p.data(), p.size(), 9, 0, kNow, &au));
  CHECK(b.peers().live(0) == 0 && b.roster().size() == 0);
  CHECK(b.counters().rejected == 1);
  p = sealed_audio(kKey, 5, 1, Codec::kOpus, 40, kNow - 120);
  CHECK(!b.on_datagram(p.data(), p.size(), 9, 0, kNow, &au));
  CHECK(b.counters().stale == 1 && b.roster().size() == 0);
  CHECK(b.peers().live(0) == 1);  // It did open: a crew member's address
  const uint8_t junk[5] = {'P', 'T', 4, 0, 0};
  CHECK(!b.on_datagram(junk, sizeof(junk), 9, 0, kNow, &au));
  CHECK(b.counters().malformed == 1);
}

TEST(net_our_own_packets_coming_back_are_dropped_unopened) {
  NetNode a(kKey, 1, config("Station"));
  uint8_t buf[NetNode::kMaxHello];
  const size_t n = a.build_hello(kNow, buf);
  NetNode::Audio au;
  CHECK(!a.on_datagram(buf, n, 1, 0, kNow, &au));
  CHECK(a.roster().size() == 0 && a.peers().live(0) == 0);
  CHECK(a.counters().duplicates == 1);
}

TEST(net_the_bridge_forwards_a_first_copy_to_the_other_transport) {
  NetNode::Config c = config("Station");
  c.relay = true;
  NetNode b(kKey, 2, c);
  NetNode::Audio au;
  NetNode::Relay relay;
  // From the WLAN: on to Reticulum, one hop spent.
  auto p = sealed_audio(kKey, 5, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(relay.to_reticulum && !relay.to_lan && !relay.hello);
  CHECK(relay.sender == 5 && relay.len == p.size());
  Header h;
  CHECK(packet::parse(relay.data, relay.len, &h) && h.ttl == 3 && h.hops == 4);
  // The copy that comes back over Reticulum: neither played nor forwarded.
  std::vector<uint8_t> back(relay.data, relay.data + relay.len);
  CHECK(!b.on_packet(back.data(), back.size(), NetNode::Via::kReticulum, 0, 0, kNow, &au, &relay));
  CHECK(!relay.to_lan && !relay.to_reticulum);
  // From Reticulum: on to the WLAN, and no address learned from it.
  p = sealed_audio(kKey, 6, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kReticulum, 0, 0, kNow, &au, &relay));
  CHECK(relay.to_lan && !relay.to_reticulum);
  CHECK(b.peers().live(0) == 1);  // Only the WLAN sender's
  CHECK(b.counters().relayed == 2);
}

TEST(net_the_bridge_keeps_the_hop_rule_and_is_off_unless_asked) {
  NetNode::Config c = config("Station");
  c.relay = true;
  c.max_hops = 4;
  NetNode b(kKey, 2, c);
  NetNode::Audio au;
  NetNode::Relay relay;
  // A packet already three relays from its origin goes no further.
  auto p = sealed_audio(kKey, 5, 1);
  packet::set_ttl(p.data(), 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(!relay.to_reticulum && !relay.to_lan);
  // Off: nothing forwarded, the audio still played.
  NetNode off(kKey, 3, config("Station"));
  p = sealed_audio(kKey, 5, 2);
  CHECK(off.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(!relay.to_reticulum && off.counters().relayed == 0);
}

TEST(net_a_hello_over_reticulum_marks_its_member_and_says_so_in_ours) {
  NetNode a(kKey, 1, config("Ashore"));
  a.set_transports(Hello::kReticulum);
  NetNode b(kKey, 2, config("Station"));
  uint8_t buf[NetNode::kMaxHello];
  const size_t n = a.build_hello(kNow, buf);
  NetNode::Audio au;
  NetNode::Relay relay;
  CHECK(!b.on_packet(buf, n, NetNode::Via::kReticulum, 0, 0, kNow, &au, &relay));
  std::vector<Member> m;
  b.roster().snapshot(0, &m);
  CHECK(m.size() == 1 && m[0].reticulum && m[0].hops == 0);
  CHECK(m.size() == 1 && m[0].transports == Hello::kReticulum);
  CHECK(b.peers().live(0) == 0);
}
