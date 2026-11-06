// SPDX-License-Identifier: EUPL-1.2
// Seal and open, beyond the vector: as the app's ChannelCryptoTest.
#include <string.h>

#include <vector>

#include "core/channel_crypto.h"
#include "hex.h"
#include "test.h"

using namespace crewradio;

namespace {

const uint8_t kKey[32] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                          17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};

}  // namespace

TEST(crypto_seals_with_a_fresh_nonce_and_opens_its_own) {
  ChannelCrypto c(kKey);
  const uint8_t aad[4] = {9, 9, 9, 9};
  const uint8_t plain[5] = {'h', 'e', 'l', 'l', 'o'};
  uint8_t a[5 + ChannelCrypto::kOverhead], b[sizeof(a)];
  CHECK(c.seal(aad, 4, plain, 5, a));
  CHECK(c.seal(aad, 4, plain, 5, b));
  CHECK(memcmp(a, b, ChannelCrypto::kNonceBytes) != 0);  // Never the same nonce
  uint8_t out[sizeof(a)];
  CHECK(c.open(aad, 4, a, sizeof(a), out) == 5);
  CHECK(memcmp(out, plain, 5) == 0);
}

TEST(crypto_refuses_another_key_other_aad_and_any_flipped_bit) {
  ChannelCrypto c(kKey);
  uint8_t other_key[32];
  memcpy(other_key, kKey, 32);
  other_key[0] ^= 1;
  ChannelCrypto other(other_key);
  const uint8_t aad[2] = {1, 2};
  const uint8_t plain[3] = {7, 8, 9};
  uint8_t s[3 + ChannelCrypto::kOverhead], out[sizeof(s)];
  CHECK(c.seal(aad, 2, plain, 3, s));
  CHECK(other.open(aad, 2, s, sizeof(s), out) == -1);
  const uint8_t aad2[2] = {1, 3};
  CHECK(c.open(aad2, 2, s, sizeof(s), out) == -1);
  for (size_t i = 0; i < sizeof(s); i++) {
    s[i] ^= 0x01;
    CHECK(c.open(aad, 2, s, sizeof(s), out) == -1);
    s[i] ^= 0x01;
  }
  CHECK(c.open(aad, 2, s, sizeof(s), out) == 3);
  CHECK(c.open(aad, 2, s, ChannelCrypto::kOverhead, out) == -1);  // No body
  CHECK(c.open(aad, 2, nullptr, 40, out) == -1);
}

TEST(crypto_pbkdf2_matches_pythons_hashlib) {
  // hashlib.pbkdf2_hmac("sha256", b"x", b"CrewRadio channel key v4", n, 32),
  // computed independently of this code (and of the app's).
  uint8_t out[32];
  CHECK(ChannelCrypto::derive((const uint8_t*)"x", 1, out, 1));
  CHECK(hex(out, 32) ==
        "9d608bd0ff658e449a25039ded581e2b44870f5ad718b10889eaa02bcc889a49");
  CHECK(ChannelCrypto::derive((const uint8_t*)"x", 1, out, 2));
  CHECK(hex(out, 32) ==
        "8b84d8262f5e79ed20c65e98fa2fd8c14fbef299b421047729fd6fd19de4f092");
  CHECK(ChannelCrypto::derive((const uint8_t*)"x", 1, out, 1000));
  CHECK(hex(out, 32) ==
        "f02f30a3cb5a92fae88d77406ef9f4d044e46e5536107236fafbc83fb79f37d4");
  CHECK(!ChannelCrypto::derive((const uint8_t*)"x", 1, out, 0));
}

TEST(crypto_an_empty_passphrase_is_one_zero_byte) {
  uint8_t empty[32], zero[32];
  const uint8_t z = 0;
  CHECK(ChannelCrypto::derive(nullptr, 0, empty, 3));
  CHECK(ChannelCrypto::derive(&z, 1, zero, 3));
  CHECK(memcmp(empty, zero, 32) == 0);
  CHECK(hex(empty, 32) ==  // hashlib with b"" and 3 iterations
        "eb7ae0bdaff3d5ce25aec3766e9b05ab41752e64255d6432671a6ced72774d13");
}
