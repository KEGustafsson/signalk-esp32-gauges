// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The primitives Reticulum names as its own, apart from the curves
// (curve25519.h): SHA-256, the truncated hash, HKDF-SHA256 and the
// encrypted token, as the app's RnsCrypto.kt:
//
//   token = iv (16) | AES-256-CBC(enc key, iv, pkcs7(plain)) |
//           HMAC-SHA256(sign key, iv | ciphertext) (32)
//
// with a 64-byte key split sign-first (bytes 0-31 authenticate, 32-63
// encrypt) and none of Fernet's version or timestamp fields.
//
// CBC is Reticulum's, not a choice made here. What makes CBC with PKCS#7
// dangerous is a padding oracle, a receiver that says whether the padding
// was good before anyone knows the ciphertext is genuine. So the padding is
// written and removed here, the removal only after the HMAC over
// iv | ciphertext has matched in constant time: a forged or altered token is
// refused on the MAC alone, whatever its padding.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace rns {

constexpr size_t kHashBytes = 16;  // The truncated hash
constexpr size_t kIvBytes = 16;
constexpr size_t kMacBytes = 32;
// Bytes a token adds before padding.
constexpr size_t kTokenOverhead = kIvBytes + kMacBytes;

// Up to three parts hashed as one.
struct Part {
  const uint8_t* p;
  size_t len;
};

// Each returns false, with its output zeroed, when mbedTLS fails: a caller
// that checks a MAC or derives a key must then refuse, never compare or use
// what the buffer happens to hold.
bool sha256(uint8_t out[32], const Part* parts, size_t n);
bool sha256(uint8_t out[32], const uint8_t* p, size_t len);
// Reticulum's truncated hash: the first 16 bytes of SHA-256.
bool truncated_hash(uint8_t out[kHashBytes], const Part* parts, size_t n);
bool truncated_hash(uint8_t out[kHashBytes], const uint8_t* p, size_t len);
// HMAC-SHA256 over up to four parts, with no allocation.
[[nodiscard]] bool hmac_sha256(uint8_t out[32], const uint8_t* key,
                               size_t key_len, const Part* parts, size_t n);

// HKDF-SHA256 (RFC 5869) into out (len <= 255 * 32); an empty salt is the
// RFC's string of zeros.
[[nodiscard]] bool hkdf(uint8_t* out, size_t len, const uint8_t* ikm,
                        size_t ikm_len, const uint8_t* salt, size_t salt_len,
                        const uint8_t* info = nullptr, size_t info_len = 0);

// The token's length for `len` bytes of plaintext.
constexpr size_t token_len(size_t len) {
  return kTokenOverhead + (len / 16 + 1) * 16;
}
// A token for `plain` under a 64-byte key into out (token_len(len) bytes);
// `iv` only for test vectors, else random. Its length, or 0 when the
// cipher or the MAC failed.
size_t token_encrypt(const uint8_t key[64], const uint8_t* plain, size_t len,
                     uint8_t* out, const uint8_t* iv = nullptr);
// The plaintext into out (room for len - kTokenOverhead bytes: the padding
// is decrypted there too): its length, or -1 when the token does not
// authenticate or unpad, without saying which.
int token_decrypt(const uint8_t key[64], const uint8_t* token, size_t len,
                  uint8_t* out);

// PKCS#7 removal, for the tests: the data length without its padding, or -1
// when the padding is not well formed. Only ever called on an authenticated
// token.
int unpad_len(const uint8_t* padded, size_t len);

// Constant-time comparison.
bool equal(const uint8_t* a, const uint8_t* b, size_t len);

}  // namespace rns
}  // namespace crewradio
