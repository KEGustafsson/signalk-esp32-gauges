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
                           int64_t time = kNow, uint8_t ttl = 4) {
  Header h;
  h.sender_id = sender;
  h.seq = seq;
  h.codec = codec;
  h.ttl = ttl;
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
  CHECK(b.peers().live(0) == 0);  // Opened, but stale: anyone can replay it
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

// On Reticulum and nobody else on the boat: this node is the forwarder.
constexpr uint8_t kForwarding =
    Hello::kLan | Hello::kReticulum | Hello::kRnsUp | Hello::kFwdAvoid;

// Net the bridge forwards a first copy to the other transport.
TEST(net_the_bridge_forwards_a_first_copy_to_the_other_transport) {
  NetNode::Config c = config("Station");
  c.relay = true;
  NetNode b(kKey, 2, c);
  b.set_transports(kForwarding);
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

// Net the bridge keeps the hop rule and is off unless asked.
TEST(net_the_bridge_keeps_the_hop_rule_and_is_off_unless_asked) {
  NetNode::Config c = config("Station");
  c.relay = true;
  c.max_hops = 4;
  NetNode b(kKey, 2, c);
  b.set_transports(kForwarding);
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

TEST(net_a_replayed_copy_from_another_address_teaches_nothing) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  auto p = sealed_audio(kKey, 5, 1);
  CHECK(b.on_datagram(p.data(), p.size(), 0x0A000001, 0, kNow, &au));
  // The same packet again from addresses of a replayer's choosing.
  for (uint32_t a = 0x0A000010; a < 0x0A000030; a++) {
    CHECK(!b.on_datagram(p.data(), p.size(), a, 0, kNow, &au));
  }
  uint32_t addrs[LanPeers::kMax];
  CHECK(b.peers().live_addrs(0, addrs) == 1 && addrs[0] == 0x0A000001);
}

TEST(net_a_missed_fresh_packet_replayed_from_spoofed_addresses_moves_one_entry) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  // Two crew members known.
  auto p = sealed_audio(kKey, 5, 1);
  b.on_datagram(p.data(), p.size(), 0x0A000005, 0, kNow, &au);
  p = sealed_audio(kKey, 6, 1);
  b.on_datagram(p.data(), p.size(), 0x0A000006, 0, kNow, &au);
  // Packets of sender 5 this node never received, replayed by someone else
  // from addresses of their choosing: each is a first copy, so accepted.
  for (int32_t seq = 2; seq < 40; seq++) {
    p = sealed_audio(kKey, 5, seq);
    b.on_datagram(p.data(), p.size(), 0x0B000000 + (uint32_t)seq, 0, kNow, &au);
  }
  uint32_t addrs[LanPeers::kMax];
  const int n = b.peers().live_addrs(0, addrs);
  bool six = false;
  for (int i = 0; i < n; i++) six = six || addrs[i] == 0x0A000006;
  CHECK(n == 2 && six);  // Sender 6 kept; sender 5's one entry moved
}

// A host with no key that sends faster than the crew spends its own budget,
// not the crew's: the crew's next frames still play (the app's
// SourceLimiter; before it, 2000 junk packets took the whole global burst).
TEST(net_a_keyless_flood_from_one_host_does_not_silence_the_crew) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  const uint8_t other[32] = {9};
  for (int i = 0; i < 5000; i++) {
    auto junk = sealed_audio(other, 77, i);
    b.on_datagram(junk.data(), junk.size(), 0x0A000063, 0, kNow, &au);
  }
  CHECK(b.counters().flooded == 4000);  // Past its burst of 1000
  int played = 0;
  for (int32_t seq = 1; seq <= 50; seq++) {
    auto p = sealed_audio(kKey, 5, seq);
    played += b.on_datagram(p.data(), p.size(), 0x0A000005, 0, kNow, &au) ? 1 : 0;
  }
  CHECK(played == 50);
}

// A copy a bridging phone relayed (ttl below the sender's hops) teaches
// nothing about where the sender is: it came from the relay's address.
TEST(net_a_relayed_first_copy_does_not_move_its_senders_address) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  auto p = sealed_audio(kKey, 5, 1);
  CHECK(b.on_datagram(p.data(), p.size(), 0x0A000005, 0, kNow, &au));
  p = sealed_audio(kKey, 5, 2, Codec::kOpus, 40, kNow, /*ttl=*/3);
  CHECK(b.on_datagram(p.data(), p.size(), 0x0A0000AA, 20, kNow, &au));  // Played
  uint32_t addrs[LanPeers::kMax];
  CHECK(b.peers().live_addrs(20, addrs) == 1 && addrs[0] == 0x0A000005);
}

// A late frame (its slot concealed already) replayed from elsewhere is
// refused before it can re-point the sender's copies.
TEST(net_a_late_frame_teaches_no_address) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  auto p = sealed_audio(kKey, 5, 1);
  b.on_datagram(p.data(), p.size(), 0x0A000005, 0, kNow, &au);
  p = sealed_audio(kKey, 5, 3);
  b.on_datagram(p.data(), p.size(), 0x0A000005, 20, kNow, &au);
  p = sealed_audio(kKey, 5, 2);  // Missed, now late
  CHECK(!b.on_datagram(p.data(), p.size(), 0x0B000001, 40, kNow, &au));
  CHECK(b.counters().late == 1);
  uint32_t addrs[LanPeers::kMax];
  CHECK(b.peers().live_addrs(40, addrs) == 1 && addrs[0] == 0x0A000005);
}

// A hello that opens but does not decode says nothing about its sender: no
// roster entry (the app ignores it too).
TEST(net_a_hello_that_does_not_decode_makes_no_member) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  auto p = sealed_audio(kKey, 5, 1, Codec::kHello, 3);  // Not a hello body
  CHECK(!b.on_datagram(p.data(), p.size(), 0x0A000005, 0, kNow, &au));
  CHECK(b.counters().hellos == 1);
  std::vector<Member> m;
  b.roster().snapshot(0, &m);
  CHECK(m.empty());
}

// A member heard only by audio, talking cleanly, keeps its bars (measured
// from its last packet until it says hello, as the app does).
TEST(net_an_audio_only_member_talking_cleanly_keeps_its_bars) {
  NetNode b(kKey, 2, config("Phone"));
  NetNode::Audio au;
  for (int32_t seq = 1; seq <= 250; seq++) {  // 5 s of talk
    auto p = sealed_audio(kKey, 5, seq);
    b.on_datagram(p.data(), p.size(), 0x0A000005, seq * 20, kNow, &au);
  }
  std::vector<Member> m;
  b.roster().snapshot(5000, &m);
  CHECK(m.size() == 1 && m[0].bars == 4);
}

// ---- one forwarder per boat (forwarder.h) --------------------------------

namespace {
// A hello from `sender` saying `transports`, as it would come straight over
// the WLAN.
std::vector<uint8_t> hello_from(int32_t sender, uint8_t transports) {
  NetNode n(kKey, sender, config("Peer"));
  n.set_transports(transports);
  uint8_t buf[NetNode::kMaxHello];
  const size_t len = n.build_hello(kNow, buf);
  return std::vector<uint8_t>(buf, buf + len);
}
}  // namespace

// Net only the forwarder bridges audio.
TEST(net_only_the_forwarder_bridges_audio) {
  NetNode::Config c = config("Station");
  c.relay = true;
  NetNode b(kKey, 2, c);
  b.set_transports(kForwarding);
  NetNode::Audio au;
  NetNode::Relay relay;
  // The plugin on the same WLAN, on Reticulum: it forwards, not us.
  auto hello = hello_from(900, Hello::kLan | Hello::kReticulum |
                                   Hello::kRnsUp | Hello::kFwdPrefer);
  b.on_packet(hello.data(), hello.size(), NetNode::Via::kLan, 9, 0, kNow, &au,
              &relay);
  CHECK(!b.is_forwarder(0));
  CHECK(relay.to_reticulum);  // Hellos keep their relay
  auto p = sealed_audio(kKey, 5, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(!relay.to_reticulum && !relay.to_lan);  // Played, not bridged
  p = sealed_audio(kKey, 6, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kReticulum, 0, 0, kNow, &au, &relay));
  CHECK(!relay.to_lan);
  // The plugin quiet for kLocalMs: this node forwards again.
  CHECK(b.is_forwarder(Forwarder::kLocalMs));
}

TEST(net_the_forwarder_leaves_a_reticulum_talker_to_itself) {
  NetNode::Config c = config("Station");
  c.relay = true;
  NetNode b(kKey, 2, c);
  b.set_transports(kForwarding);
  NetNode::Audio au;
  NetNode::Relay relay;
  // Another node on the WLAN, on Reticulum too; both would rather not
  // forward, and the lower id does.
  auto hello = hello_from(70, Hello::kLan | Hello::kRnsUp | Hello::kFwdAvoid);
  b.on_packet(hello.data(), hello.size(), NetNode::Via::kLan, 9, 0, kNow, &au,
              &relay);
  CHECK(b.is_forwarder(0));  // 2 < 70
  auto p = sealed_audio(kKey, 70, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(!relay.to_reticulum);  // 70 sends its own over Reticulum
  // A talker on the WLAN alone: bridged.
  p = sealed_audio(kKey, 71, 1);
  CHECK(b.on_packet(p.data(), p.size(), NetNode::Via::kLan, 9, 0, kNow, &au, &relay));
  CHECK(relay.to_reticulum);
}

TEST(net_a_wlan_hello_heard_first_over_reticulum_still_makes_it_local) {
  NetNode b(kKey, 2, config("Station"));
  NetNode::Audio au;
  NetNode::Relay relay;
  auto hello = hello_from(70, Hello::kLan | Hello::kRnsUp);
  b.on_packet(hello.data(), hello.size(), NetNode::Via::kReticulum, 0, 0, kNow,
              &au, &relay);
  CHECK(!b.forwarder().is_local(70, 0));
  b.on_packet(hello.data(), hello.size(), NetNode::Via::kLan, 9, 0, kNow, &au,
              &relay);  // The same hello, a copy now
  CHECK(b.forwarder().is_local(70, 0));
}

TEST(net_learns_who_is_at_the_far_end_of_a_link) {
  NetNode b(kKey, 2, config("Station"));
  NetNode::Audio au;
  NetNode::Relay relay;
  const uint32_t tag = Forwarder::make_tag(4, 1);
  // A bridge's copy (ttl spent by one) says nothing about the link.
  auto relayed = sealed_audio(kKey, 70, 1, Codec::kOpus, 40, kNow, 3);
  b.on_packet(relayed.data(), relayed.size(), NetNode::Via::kReticulum, 0, 0,
              kNow, &au, &relay, tag);
  auto hello = hello_from(70, Hello::kLan | Hello::kRnsUp);
  b.on_packet(hello.data(), hello.size(), NetNode::Via::kLan, 9, 0, kNow, &au,
              &relay);
  CHECK(b.forwarder().skip_tag(4, 0) == 0);
  // Straight from 70 on that link: 70 is local, so no copies on it.
  auto p = sealed_audio(kKey, 70, 2);
  b.on_packet(p.data(), p.size(), NetNode::Via::kReticulum, 0, 0, kNow, &au,
              &relay, tag);
  CHECK(b.forwarder().skip_tag(4, 0) == tag);
  // A stranger's packet on the link teaches nothing.
  const uint8_t other[32] = {9};
  auto junk = sealed_audio(other, 71, 1);
  b.on_packet(junk.data(), junk.size(), NetNode::Via::kReticulum, 0, 0, kNow,
              &au, &relay, Forwarder::make_tag(5, 1));
  CHECK(b.forwarder().skip_tag(5, 0) == 0);
}
