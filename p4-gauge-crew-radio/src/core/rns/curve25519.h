// SPDX-License-Identifier: EUPL-1.2
#pragma once

// X25519 (RFC 7748) and Ed25519 (RFC 8032) by hand, ported from the
// crew-radio app's Curve25519.kt: mbedTLS 3.6 has no Ed25519, and one
// implementation of the field for both curves is easier to trust than two.
//
// The arithmetic is the public-domain TweetNaCl design: a field element is
// sixteen 16-bit limbs, multiplication folds the high half back with 38
// (2^256 = 38 mod p), and every secret-dependent choice is a masked swap
// rather than a branch, so the ladder and the base-point multiplication take
// the same path for every key. The curve constants are written out here (the
// app computes them with BigInteger); tests/test_curve25519.cpp checks each
// against its definition with this same field arithmetic, and the whole
// against the RFC vectors and the app's rns.vector.json. SHA-512 is mbedTLS's.
//
// Speed, not size, is what TweetNaCl gives up: on the ESP32-P4 a signature
// check is some tens of milliseconds. The Reticulum task does this work at a
// priority below the radio's audio and network tasks, and a stranger can
// make it do only so much (core/rns/reticulum_node.h).

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace rns {
namespace curve25519 {

// X25519(scalar, u): the shared secret, or the public key for u = 9.
void x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t u[32]);
void x25519_public(uint8_t out[32], const uint8_t scalar[32]);

void ed25519_public(uint8_t out[32], const uint8_t seed[32]);
// `pub` is ed25519_public(seed), passed in so a signature costs one scalar
// multiplication, not two.
void ed25519_sign(uint8_t sig[64], const uint8_t seed[32],
                  const uint8_t pub[32], const uint8_t* msg, size_t len);
// True only for a canonical signature by `pub` over `msg`: S at or above
// the group order is refused, as OpenSSL does.
bool ed25519_verify(const uint8_t pub[32], const uint8_t* msg, size_t len,
                    const uint8_t sig[64]);

// For the tests of the constants: the field's own operations on public
// values, limbs as above.
namespace field {
using Fe = int64_t[16];
void mul(Fe o, const Fe a, const Fe b);
void inv(Fe o, const Fe i);
void pack(uint8_t o[32], const Fe n);
void unpack(Fe o, const uint8_t n[32]);
extern const Fe kD, kD2, kSqrtM1, kBx, kBy;
}  // namespace field

}  // namespace curve25519
}  // namespace rns
}  // namespace crewradio
