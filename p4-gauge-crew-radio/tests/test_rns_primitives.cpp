// SPDX-License-Identifier: EUPL-1.2
// Reticulum's pieces, as the app's RnsPrimitivesTest and AnnounceBudgetTest:
// HKDF, the token and its padding, packets and their framing, announces,
// links, the Carry framing and rnsd's announce rate rule.
#include <string.h>

#include <vector>

#include "core/rns/announce_budget.h"
#include "core/rns/carry.h"
#include "core/rns/rns_crypto.h"
#include "core/rns/rns_identity.h"
#include "core/rns/rns_link.h"
#include "core/rns/rns_packet.h"
#include "hex.h"
#include "test.h"

using namespace crewradio::rns;

namespace {

std::vector<uint8_t> seq(int start, size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; i++) v[i] = (uint8_t)(start + (int)i);
  return v;
}

std::vector<uint8_t> packet_of(uint8_t type, uint8_t dest_type,
                               const uint8_t* dest, const std::vector<uint8_t>& data,
                               const uint8_t* transport = nullptr,
                               bool context_flag = false, uint8_t hops = 0,
                               uint8_t context = kCtxNone) {
  uint8_t out[kMtu];
  const size_t n = encode(out, type, dest_type, dest, context, data.data(),
                          data.size(), transport, context_flag, hops);
  return std::vector<uint8_t>(out, out + n);
}

Packet parse(const std::vector<uint8_t>& raw) {
  Packet p;
  CHECK(decode(raw.data(), raw.size(), &p));
  return p;
}

// An announce of `id` under nh, parsed back.
Announce announced(const Identity& id, const uint8_t* nh) {
  uint8_t dest[kHashBytes], data[kAnnounceMin];
  const size_t n = build_announce(id, nh, nullptr, 0, 1000, dest, data);
  const auto raw = packet_of(kAnnounce, kSingle, dest, std::vector<uint8_t>(data, data + n));
  Announce a;
  CHECK(parse_announce(parse(raw), &a));
  return a;
}

}  // namespace

TEST(rns_hkdf_matches_rfc5869_case_1) {
  const std::vector<uint8_t> ikm(22, 0x0b);
  const auto salt = seq(0, 13);
  const auto info = seq(0xf0, 10);
  uint8_t okm[42];
  CHECK(hkdf(okm, sizeof(okm), ikm.data(), ikm.size(), salt.data(),
             salt.size(), info.data(), info.size()));
  CHECK(hex(okm, sizeof(okm)) ==
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
}

// RFC 4231's HMAC-SHA256 cases 1, 2 and 6 (a key longer than the block,
// which is hashed first), one of them split across parts.
TEST(rns_hmac_matches_rfc4231) {
  uint8_t mac[32];
  const std::vector<uint8_t> k1(20, 0x0b);
  const char* d1 = "Hi There";
  const Part p1 = {(const uint8_t*)d1, strlen(d1)};
  CHECK(hmac_sha256(mac, k1.data(), k1.size(), &p1, 1));
  CHECK(hex(mac, 32) ==
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

  const char* k2 = "Jefe";
  const char* d2a = "what do ya want ";
  const char* d2b = "for nothing?";
  const Part p2[2] = {{(const uint8_t*)d2a, strlen(d2a)},
                      {(const uint8_t*)d2b, strlen(d2b)}};
  CHECK(hmac_sha256(mac, (const uint8_t*)k2, strlen(k2), p2, 2));
  CHECK(hex(mac, 32) ==
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

  const std::vector<uint8_t> k6(131, 0xaa);
  const char* d6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  const Part p6 = {(const uint8_t*)d6, strlen(d6)};
  CHECK(hmac_sha256(mac, k6.data(), k6.size(), &p6, 1));
  CHECK(hex(mac, 32) ==
        "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54");
}

// More parts than it takes is a refusal with nothing in the output, never a
// MAC over some of them.
TEST(rns_hmac_refuses_too_many_parts) {
  uint8_t mac[32];
  memset(mac, 0x5A, sizeof(mac));
  const uint8_t b = 1;
  const Part parts[5] = {{&b, 1}, {&b, 1}, {&b, 1}, {&b, 1}, {&b, 1}};
  CHECK(!hmac_sha256(mac, &b, 1, parts, 5));
  for (uint8_t v : mac) CHECK(v == 0);
}

TEST(rns_a_token_opens_only_with_its_key_and_untouched) {
  const auto key = seq(1, 64);
  const uint8_t plain[] = {'h', 'e', 'l', 'l', 'o'};
  uint8_t t[token_len(sizeof(plain))];
  const size_t n = token_encrypt(key.data(), plain, sizeof(plain), t);
  CHECK(n == sizeof(t));
  uint8_t out[64];
  CHECK(token_decrypt(key.data(), t, n, out) == 5 && memcmp(out, plain, 5) == 0);
  for (size_t i : {(size_t)0, (size_t)20, n - 1}) {
    uint8_t bad[sizeof(t)];
    memcpy(bad, t, n);
    bad[i] ^= 1;
    CHECK(token_decrypt(key.data(), bad, n, out) == -1);
  }
  const auto other = seq(2, 64);
  CHECK(token_decrypt(other.data(), t, n, out) == -1);
  CHECK(token_decrypt(key.data(), t, 40, out) == -1);
}

TEST(rns_padding_is_pkcs7_and_malformed_padding_is_refused) {
  CHECK(token_len(0) == kTokenOverhead + 16);
  CHECK(token_len(15) == kTokenOverhead + 16);
  CHECK(token_len(16) == kTokenOverhead + 32);
  std::vector<uint8_t> zero(16, 0), past(16, 17), disagree(16, 0);
  disagree[14] = 1;
  disagree[15] = 2;
  CHECK(unpad_len(nullptr, 0) == -1);
  std::vector<uint8_t> short_block(15, 1);
  CHECK(unpad_len(short_block.data(), short_block.size()) == -1);
  CHECK(unpad_len(zero.data(), 16) == -1);  // A count of 0
  CHECK(unpad_len(past.data(), 16) == -1);  // Past the block
  CHECK(unpad_len(disagree.data(), 16) == -1);
  // Every length round trips through a token.
  const auto key = seq(5, 64);
  for (size_t len = 0; len <= 40; len++) {
    const auto plain = seq(0, len);
    uint8_t t[token_len(40)], out[64];
    const size_t n = token_encrypt(key.data(), plain.data(), len, t);
    CHECK(token_decrypt(key.data(), t, n, out) == (int)len);
    CHECK(len == 0 || memcmp(out, plain.data(), len) == 0);
  }
}

TEST(rns_packets_round_trip_and_the_hash_ignores_transport) {
  const auto dest = seq(0x40, 16);
  const auto transport = seq(0x80, 16);
  const auto one = packet_of(kLinkRequest, kSingle, dest.data(), seq(0, 64));
  const auto two = packet_of(kLinkRequest, kSingle, dest.data(), seq(0, 64),
                             transport.data(), false, 3);
  const Packet a = parse(one);
  const Packet b = parse(two);
  CHECK(a.header_type == kHeader1);
  CHECK(b.header_type == kHeader2);
  CHECK(memcmp(b.transport_id, transport.data(), 16) == 0);
  CHECK(b.hops == 3);
  CHECK(memcmp(a.destination, b.destination, 16) == 0);
  CHECK(a.data_len == 64 && b.data_len == 64);
  uint8_t ha[32], hb[32];
  hash_of(ha, one.data(), one.size());
  hash_of(hb, two.data(), two.size());
  CHECK(memcmp(ha, hb, 32) == 0);
  Packet p;
  uint8_t five[5] = {};
  CHECK(!decode(five, sizeof(five), &p));
  auto ifac = one;
  ifac[0] |= 0x80;
  CHECK(!decode(ifac.data(), ifac.size(), &p));  // Interface access codes
  std::vector<uint8_t> huge(kMtu + 1, 0);
  CHECK(!decode(huge.data(), huge.size(), &p));
  CHECK(!decode(two.data(), 20, &p));
  uint8_t out[kMtu];
  const std::vector<uint8_t> too_much(kMtu, 0);
  CHECK(encode(out, kData, kLink, dest.data(), kCtxNone, too_much.data(),
               too_much.size()) == 0);
}

TEST(rns_hdlc_escapes_and_drops_junk_and_oversized_frames) {
  std::vector<std::vector<uint8_t>> got;
  auto on_frame = [&](const uint8_t* f, size_t n) { got.emplace_back(f, f + n); };
  Deframer d;
  const uint8_t packet[] = {1, 0x7e, 2, 0x7d, 3};
  uint8_t framed[kMaxFrame];
  const size_t n = frame(packet, sizeof(packet), framed);
  CHECK(hex(framed, n) == "7e017d5e027d5d037e");
  const uint8_t junk[] = {9, 9, 9};
  d.push(junk, sizeof(junk), on_frame);
  d.push(framed, 4, on_frame);
  d.push(framed + 4, n - 4, on_frame);
  std::vector<uint8_t> big(Deframer::kMax + 10, 1);
  static uint8_t big_framed[2 * (Deframer::kMax + 10) + 2];
  d.push(big_framed, frame(big.data(), big.size(), big_framed), on_frame);
  const uint8_t five[] = {5};
  uint8_t small[8];
  d.push(small, frame(five, 1, small), on_frame);
  CHECK(got.size() == 2);
  CHECK(got.size() == 2 && got[0] == std::vector<uint8_t>(packet, packet + 5));
  CHECK(got.size() == 2 && got[1] == std::vector<uint8_t>(1, 5));
}

TEST(rns_announces_tampered_foreign_or_with_a_ratchet) {
  const Identity id = Identity::generate();
  uint8_t nh[kNameHashBytes];
  name_hash("crewradio.channel.test", nh);
  const uint8_t app[] = {'a', 'p', 'p'};
  uint8_t dest[kHashBytes], data[kAnnounceMin + 3];
  const size_t n = build_announce(id, nh, app, sizeof(app), 1234, dest, data);
  const std::vector<uint8_t> body(data, data + n);
  const auto raw = packet_of(kAnnounce, kSingle, dest, body);
  Announce a;
  CHECK(parse_announce(parse(raw), &a));
  CHECK(a.emitted == 1234);
  auto tampered = raw;
  tampered.back() ^= 1;
  CHECK(!parse_announce(parse(tampered), &a));
  const auto elsewhere = seq(0, 16);
  CHECK(!parse_announce(parse(packet_of(kAnnounce, kSingle, elsewhere.data(), body)), &a));
  // A ratchet, signed in: still an announce.
  const auto ratchet = seq(0x33, 32);
  std::vector<uint8_t> msg(dest, dest + 16);
  msg.insert(msg.end(), id.public_key(), id.public_key() + 64);
  msg.insert(msg.end(), nh, nh + 10);
  msg.insert(msg.end(), data + 74, data + 84);
  msg.insert(msg.end(), ratchet.begin(), ratchet.end());
  uint8_t sig[64];
  id.sign(msg.data(), msg.size(), sig);
  std::vector<uint8_t> with(id.public_key(), id.public_key() + 64);
  with.insert(with.end(), nh, nh + 10);
  with.insert(with.end(), data + 74, data + 84);
  with.insert(with.end(), ratchet.begin(), ratchet.end());
  with.insert(with.end(), sig, sig + 64);
  CHECK(parse_announce(parse(packet_of(kAnnounce, kSingle, dest, with, nullptr, true)), &a));
}

TEST(rns_links_refuse_an_impostor_and_carry_data_keepalive_and_close) {
  const Identity dest = Identity::generate();
  const Identity impostor = Identity::generate();
  uint8_t nh[kNameHashBytes];
  name_hash("crewradio.channel.test", nh);
  const Announce a = announced(dest, nh);
  Request req;
  request(a, nullptr, &req);
  const std::vector<uint8_t> raw(req.raw, req.raw + req.raw_len);
  const Packet request_packet = parse(raw);

  Link link;
  uint8_t proof[kMtu];
  size_t proof_len = 0;
  CHECK(accept(impostor, request_packet, &link, proof, &proof_len));
  Link mine;
  CHECK(!req.complete(parse(std::vector<uint8_t>(proof, proof + proof_len)), &mine));

  Link theirs;
  CHECK(accept(dest, request_packet, &theirs, proof, &proof_len));
  CHECK(req.complete(parse(std::vector<uint8_t>(proof, proof + proof_len)), &mine));
  CHECK(mine.active() && !theirs.active());

  uint8_t pkt[kMtu], plain[kMdu + 16], reply[kMtu];
  size_t plain_len = 0, reply_len = 0;
  auto handle = [&](Link& l, size_t n) {
    return l.handle(parse(std::vector<uint8_t>(pkt, pkt + n)), plain, &plain_len,
                    reply, &reply_len);
  };
  CHECK(handle(theirs, mine.rtt_packet(0.1, pkt)) == Link::Event::kRtt);
  CHECK(theirs.active());
  const uint8_t over[] = {'o', 'v', 'e', 'r'};
  CHECK(handle(theirs, mine.data_packet(over, 4, pkt)) == Link::Event::kData);
  CHECK(plain_len == 4 && memcmp(plain, over, 4) == 0);
  CHECK(handle(theirs, mine.keepalive_packet(false, pkt)) == Link::Event::kKeepalive);
  CHECK(reply_len > 0);
  memcpy(pkt, reply, reply_len);
  CHECK(handle(mine, reply_len) == Link::Event::kKeepalive);
  CHECK(reply_len == 0);  // An answer is not answered
  const std::vector<uint8_t> too_big(kMdu + 1, 0);
  CHECK(mine.data_packet(too_big.data(), too_big.size(), pkt) == 0);
  CHECK(handle(theirs, mine.close_packet(pkt)) == Link::Event::kClose);
  const uint8_t late[] = {'l', 'a', 't', 'e'};
  CHECK(handle(theirs, mine.data_packet(late, 4, pkt)) == Link::Event::kNone);

  // A request with MTU signalling bytes has the link id of its keys alone.
  std::vector<uint8_t> with_mtu(request_packet.data, request_packet.data + 64);
  with_mtu.push_back(0x21);
  with_mtu.push_back(0x01);
  with_mtu.push_back(0xf4);
  Link signalled;
  CHECK(accept(dest, parse(packet_of(kLinkRequest, kSingle, request_packet.destination, with_mtu)),
               &signalled, proof, &proof_len));
  CHECK(memcmp(signalled.id(), req.id, 16) == 0);
  // A low-order public key yields no link.
  Link none;
  CHECK(!accept(dest, parse(packet_of(kLinkRequest, kSingle, request_packet.destination,
                                      std::vector<uint8_t>(64, 0))),
                &none, proof, &proof_len));
}

TEST(rns_carry_whole_two_and_three_parts_and_loss_drops_only_that_packet) {
  carry::Joiner j;
  uint8_t parts[carry::kMaxParts][kMdu];
  size_t lens[carry::kMaxParts];
  const uint8_t* out = nullptr;
  size_t out_len = 0;
  auto push = [&](size_t i) { return j.push(parts[i], lens[i], &out, &out_len); };
  auto is = [&](const std::vector<uint8_t>& v) {
    return out_len == v.size() && memcmp(out, v.data(), v.size()) == 0;
  };
  const auto small = seq(0, 100);
  CHECK(carry::cut(small.data(), small.size(), 0, parts, lens) == 1);
  CHECK(push(0) && is(small));
  const auto pcm = seq(0, 686);
  CHECK(carry::cut(pcm.data(), pcm.size(), 1, parts, lens) == 2);
  CHECK(lens[0] <= kMdu && lens[1] <= kMdu);
  CHECK(!push(0));
  CHECK(push(1) && is(pcm));
  const auto big = seq(0, 1024);
  CHECK(carry::cut(big.data(), big.size(), 2, parts, lens) == 3);
  CHECK(!push(0));
  CHECK(!push(1));
  CHECK(push(2) && is(big));
  // A second half without its first, then another packet's half.
  CHECK(carry::cut(pcm.data(), pcm.size(), 1, parts, lens) == 2);
  CHECK(!push(1));
  CHECK(!push(0));
  CHECK(carry::cut(pcm.data(), pcm.size(), 3, parts, lens) == 2);
  CHECK(!push(1));
  CHECK(carry::cut(small.data(), small.size(), 4, parts, lens) == 1);
  CHECK(push(0) && is(small));
  const uint8_t odd[] = {9, 0, 0, 1};
  CHECK(!j.push(odd, sizeof(odd), &out, &out_len));
  const auto too_big = seq(0, carry::kMaxPacket + 1);
  CHECK(carry::cut(too_big.data(), too_big.size(), 5, parts, lens) == 0);
}

TEST(rns_questions_for_the_boat_are_recognised_to_be_dropped) {
  const uint8_t request_part[] = {0x81, 0, 1, 0, 1, 'x'};
  const uint8_t answer_part[] = {0x82, 0, 1, 0, 1, 0};
  const uint8_t too_short[] = {0x81, 0, 1, 0, 1};
  CHECK(carry::is_ask(request_part, sizeof(request_part)));
  CHECK(carry::is_ask(answer_part, sizeof(answer_part)));
  CHECK(!carry::is_ask(too_short, sizeof(too_short)));
  const uint8_t whole[] = {1, 2, 3, 4, 5, 6};
  CHECK(!carry::is_ask(whole, sizeof(whole)));
}

TEST(rns_announce_budget_six_at_once_then_one_an_hour) {
  AnnounceBudget b;
  int64_t t = 0;
  std::vector<bool> passed;
  for (int i = 0; i < 8; i++) {
    passed.push_back(b.record(t));
    t += 60000;
  }
  CHECK(passed == std::vector<bool>({true, true, true, true, true, true, false, false}));
  CHECK(!b.peek(t).passes);
  t = 5 * 60000 + AnnounceBudget::kTargetMs + 1;
  CHECK(b.peek(t).passes);
  CHECK(b.record(t));
  CHECK(!b.record(t + 60000));
  int64_t u = t + AnnounceBudget::kTargetMs * 2;
  for (int i = 0; i < 6; i++) {
    CHECK(b.record(u));
    u += AnnounceBudget::kTargetMs;
  }
  CHECK(b.violations() == 0);
  CHECK(AnnounceBudget::kTargetMs == 3600000);
  CHECK(AnnounceBudget::kGrace == 5);
}
