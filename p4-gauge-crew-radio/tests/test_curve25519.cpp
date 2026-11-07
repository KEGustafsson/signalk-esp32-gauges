// SPDX-License-Identifier: EUPL-1.2
// The hand-written curves against RFC 7748 and RFC 8032's own vectors, as the
// app's Curve25519Test, and the constants against their definitions.
#include <string.h>

#include <vector>

#include "core/rns/curve25519.h"
#include "hex.h"
#include "port/random.h"
#include "test.h"

using namespace crewradio::rns;
using namespace crewradio::rns::curve25519;

namespace {

bool fe_equal(const field::Fe a, const field::Fe b) {
  uint8_t x[32], y[32];
  field::pack(x, a);
  field::pack(y, b);
  return memcmp(x, y, 32) == 0;
}

void fe_small(field::Fe o, int64_t v) {
  memset(o, 0, sizeof(field::Fe));
  o[0] = v & 0xffff;
  o[1] = (v >> 16) & 0xffff;
}

// -a, as 0 - a carried.
void fe_neg(field::Fe o, const field::Fe a) {
  field::Fe zero = {0}, one = {1};
  for (int i = 0; i < 16; i++) o[i] = zero[i] - a[i];
  field::mul(o, o, one);  // Carries
}

}  // namespace

TEST(curve_constants_follow_from_their_definitions) {
  field::Fe t, u, one = {1};
  // d * 121666 = -121665
  field::Fe k666, k665, minus665;
  fe_small(k666, 121666);
  fe_small(k665, 121665);
  fe_neg(minus665, k665);
  field::mul(t, field::kD, k666);
  CHECK(fe_equal(t, minus665));
  // 2d
  field::Fe two;
  fe_small(two, 2);
  field::mul(t, field::kD, two);
  CHECK(fe_equal(t, field::kD2));
  // sqrt(-1)^2 = -1
  field::Fe minus1;
  fe_neg(minus1, one);
  field::mul(t, field::kSqrtM1, field::kSqrtM1);
  CHECK(fe_equal(t, minus1));
  // y = 4/5
  field::Fe five, four;
  fe_small(five, 5);
  fe_small(four, 4);
  field::mul(t, field::kBy, five);
  CHECK(fe_equal(t, four));
  // On the curve: -x^2 + y^2 = 1 + d x^2 y^2
  field::Fe x2, y2, lhs, rhs;
  field::mul(x2, field::kBx, field::kBx);
  field::mul(y2, field::kBy, field::kBy);
  for (int i = 0; i < 16; i++) lhs[i] = y2[i] - x2[i];
  field::mul(lhs, lhs, one);
  field::mul(u, x2, y2);
  field::mul(u, u, field::kD);
  for (int i = 0; i < 16; i++) rhs[i] = u[i] + one[i];
  field::mul(rhs, rhs, one);
  CHECK(fe_equal(lhs, rhs));
  // x is the even root
  uint8_t bx[32];
  field::pack(bx, field::kBx);
  CHECK((bx[0] & 1) == 0);
  // And the inverse is one.
  field::inv(t, five);
  field::mul(t, t, five);
  CHECK(fe_equal(t, one));
}

TEST(curve_x25519_matches_rfc7748) {
  const auto scalar = unhex("a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4");
  const auto u = unhex("e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c");
  uint8_t out[32];
  x25519(out, scalar.data(), u.data());
  CHECK(hex(out, 32) == "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552");

  const auto alice = unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
  const auto bob = unhex("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb");
  uint8_t alice_pub[32], bob_pub[32];
  x25519_public(alice_pub, alice.data());
  x25519_public(bob_pub, bob.data());
  CHECK(hex(alice_pub, 32) == "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
  CHECK(hex(bob_pub, 32) == "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
  const char* shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
  x25519(out, alice.data(), bob_pub);
  CHECK(hex(out, 32) == shared);
  x25519(out, bob.data(), alice_pub);
  CHECK(hex(out, 32) == shared);
}

TEST(curve_ed25519_matches_rfc8032) {
  const auto seed1 = unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  uint8_t pub[32], sig[64];
  ed25519_public(pub, seed1.data());
  CHECK(hex(pub, 32) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  ed25519_sign(sig, seed1.data(), pub, nullptr, 0);
  CHECK(hex(sig, 64) ==
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
  CHECK(ed25519_verify(pub, nullptr, 0, sig));

  const auto seed2 = unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
  const uint8_t msg2[1] = {0x72};
  ed25519_public(pub, seed2.data());
  CHECK(hex(pub, 32) == "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
  ed25519_sign(sig, seed2.data(), pub, msg2, 1);
  CHECK(hex(sig, 64) ==
        "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
  CHECK(ed25519_verify(pub, msg2, 1, sig));
}

TEST(curve_ed25519_refuses_what_is_not_a_signature) {
  const auto seed = unhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
  uint8_t pub[32], sig[64];
  ed25519_public(pub, seed.data());
  const uint8_t msg[] = {'c', 'r', 'e', 'w'};
  const uint8_t crow[] = {'c', 'r', 'o', 'w'};
  ed25519_sign(sig, seed.data(), pub, msg, sizeof(msg));
  CHECK(ed25519_verify(pub, msg, sizeof(msg), sig));
  CHECK(!ed25519_verify(pub, crow, sizeof(crow), sig));
  for (int i : {0, 31, 32, 63}) {
    uint8_t bad[64];
    memcpy(bad, sig, 64);
    bad[i] ^= 1;
    CHECK(!ed25519_verify(pub, msg, sizeof(msg), bad));
  }
  uint8_t sevens[32], other[32];
  memset(sevens, 7, sizeof(sevens));
  ed25519_public(other, sevens);
  CHECK(!ed25519_verify(other, msg, sizeof(msg), sig));
  // S + L: the same point equation, a different (non-canonical) encoding.
  static const uint8_t kL[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
                                 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
                                 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
  uint8_t plus_l[64];
  memcpy(plus_l, sig, 64);
  int carry = 0;
  for (int i = 0; i < 32; i++) {
    const int v = plus_l[32 + i] + kL[i] + carry;
    plus_l[32 + i] = (uint8_t)v;
    carry = v >> 8;
  }
  CHECK(carry == 0);
  CHECK(!ed25519_verify(pub, msg, sizeof(msg), plus_l));
  // A point that is not on the curve.
  uint8_t off_curve[32];
  memset(off_curve, 0xff, sizeof(off_curve));
  off_curve[31] = 0x7f;
  CHECK(!ed25519_verify(off_curve, msg, sizeof(msg), sig));
}

TEST(curve_random_keys_sign_verify_and_agree) {
  for (int round = 0; round < 20; round++) {
    uint8_t seed[32], a[32], b[32];
    crewradio::port::random_bytes(seed, 32);
    crewradio::port::random_bytes(a, 32);
    crewradio::port::random_bytes(b, 32);
    uint8_t pub[32], sig[64];
    ed25519_public(pub, seed);
    std::vector<uint8_t> msg(round * 13);
    for (size_t i = 0; i < msg.size(); i++) msg[i] = (uint8_t)(i * 7 + round);
    ed25519_sign(sig, seed, pub, msg.data(), msg.size());
    CHECK(ed25519_verify(pub, msg.data(), msg.size(), sig));
    if (!msg.empty()) {
      msg[0] ^= 0x40;
      CHECK(!ed25519_verify(pub, msg.data(), msg.size(), sig));
    }
    uint8_t pa[32], pb[32], sa[32], sb[32];
    x25519_public(pa, a);
    x25519_public(pb, b);
    x25519(sa, a, pb);
    x25519(sb, b, pa);
    CHECK(memcmp(sa, sb, 32) == 0);
  }
}
