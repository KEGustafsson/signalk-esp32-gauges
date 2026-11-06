// SPDX-License-Identifier: EUPL-1.2
// Who gets a copy on the WLAN, as the app's LanTransport.
#include "core/lan_peers.h"
#include "core/packet.h"
#include "test.h"

using namespace crewradio;

namespace {

std::vector<uint8_t> pkt(Codec codec, int32_t sender) {
  Header h;
  h.codec = codec;
  h.sender_id = sender;
  std::vector<uint8_t> p(packet::kHeader + 8, 0);
  packet::encode_header(h, p.data());
  return p;
}

LanPeers::Plan plan(LanPeers& lp, Codec c, int32_t sender, int64_t t,
                    uint32_t except = 0) {
  const auto p = pkt(c, sender);
  return lp.plan(p.data(), p.size(), t, except);
}

}  // namespace

TEST(peers_with_nobody_known_everything_goes_to_the_group) {
  LanPeers lp;
  const auto a = plan(lp, Codec::kOpus, 1, 0);
  CHECK(a.group && a.count == 0);
}

TEST(peers_audio_goes_unicast_once_a_peer_is_known_after_the_burst_floor) {
  LanPeers lp;
  lp.confirm(0x0A000002, 0);
  for (int i = 0; i < LanPeers::kBurstFrames; i++) {
    const auto a = plan(lp, Codec::kOpus, 1, i * 20);
    CHECK(a.group && a.count == 1);  // The first frames keep the group copies
  }
  const auto a = plan(lp, Codec::kOpus, 1, 200);
  CHECK(!a.group && a.count == 1 && a.addrs[0] == 0x0A000002);
  // A new burst after a 400 ms gap gets its floor again; another sender too.
  CHECK(plan(lp, Codec::kOpus, 1, 700).group);
  CHECK(plan(lp, Codec::kOpus, 2, 700).group);
}

TEST(peers_hellos_always_keep_the_group_copies) {
  LanPeers lp;
  lp.confirm(0x0A000002, 0);
  for (int i = 0; i < 10; i++) {
    const auto a = plan(lp, Codec::kHello, 1, 1000 + i);
    CHECK(a.group && a.count == 1);
  }
}

TEST(peers_expire_after_five_seconds_and_except_is_left_out) {
  LanPeers lp;
  lp.confirm(1, 0);
  lp.confirm(2, 3000);
  auto a = plan(lp, Codec::kHello, 9, 4000, 2);
  CHECK(a.count == 1 && a.addrs[0] == 1);
  CHECK(lp.live(5001) == 1);
  a = plan(lp, Codec::kHello, 9, 5001);
  CHECK(a.count == 1 && a.addrs[0] == 2);
  CHECK(lp.live(8001) == 0);
}

TEST(peers_the_table_is_bounded_and_evicts_the_stalest) {
  LanPeers lp;
  for (uint32_t a = 1; a <= LanPeers::kMax; a++) lp.confirm(a, a);
  lp.confirm(1, 100);  // 1 refreshed: 2 is now the stalest
  lp.confirm(99, 101);
  const auto p = plan(lp, Codec::kHello, 9, 102);
  CHECK(p.count == LanPeers::kMax);
  bool has2 = false, has99 = false;
  for (int i = 0; i < p.count; i++) {
    has2 = has2 || p.addrs[i] == 2;
    has99 = has99 || p.addrs[i] == 99;
  }
  CHECK(!has2 && has99);
}
