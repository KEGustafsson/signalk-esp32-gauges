// SPDX-License-Identifier: EUPL-1.2
#pragma once

// A Reticulum identity, as the app's RnsIdentity.kt: an X25519 key pair for
// key agreement and an Ed25519 key pair for signatures. Its public key is the
// two public keys in that order, its hash the truncated SHA-256 of that. A
// single destination named `app.aspect.aspect` is addressed by the truncated
// hash of its 10-byte name hash and the identity hash. An announce says which
// key answers for a destination:
//
//   public key (64) | name hash (10) | random hash (10) |
//   [ratchet (32) with the context flag] | signature (64) | app data
//
// signed over destination | public key | name hash | random hash | ratchet |
// app data, the random hash being five random bytes and the send time in
// seconds as five big-endian bytes.

#include <stddef.h>
#include <stdint.h>

#include "core/rns/rns_packet.h"

namespace crewradio {
namespace rns {

constexpr size_t kNameHashBytes = 10;
constexpr size_t kRandomHashBytes = 10;
constexpr size_t kPublicBytes = 64;
constexpr size_t kSigBytes = 64;
constexpr size_t kRatchetBytes = 32;
constexpr size_t kAnnounceMin =
    kPublicBytes + kNameHashBytes + kRandomHashBytes + kSigBytes;

class Identity {
 public:
  Identity(const uint8_t enc_priv[32], const uint8_t sig_seed[32]);
  ~Identity();
  // A fresh one, from the port's random bytes.
  static Identity generate();

  const uint8_t* public_key() const { return public_key_; }  // 64
  const uint8_t* enc_pub() const { return public_key_; }     // 32
  const uint8_t* sig_pub() const { return public_key_ + 32; }
  const uint8_t* hash() const { return hash_; }  // kHashBytes
  // False when hashing the key failed (the P4's hardware SHA needs DMA
  // RAM): its hash is then zeros, a destination nobody would accept.
  bool ok() const { return ok_; }

  void sign(const uint8_t* msg, size_t len, uint8_t sig[kSigBytes]) const;
  // The X25519 secret with peer_pub; false for a low-order key that yields
  // all zeros (OpenSSL refuses those too).
  bool agree(const uint8_t peer_pub[32], uint8_t out[32]) const;

 private:
  uint8_t enc_priv_[32];
  uint8_t sig_seed_[32];
  uint8_t public_key_[kPublicBytes];
  uint8_t hash_[kHashBytes];
  bool ok_ = false;
};

// The X25519 secret of priv and peer_pub; false when it is all zeros.
bool agree(const uint8_t priv[32], const uint8_t peer_pub[32], uint8_t out[32]);

// Both false when the hash failed (their output then zeros).
bool name_hash(const char* name, uint8_t out[kNameHashBytes]);
bool destination_hash(const uint8_t name_hash[kNameHashBytes],
                      const uint8_t identity_hash[kHashBytes],
                      uint8_t out[kHashBytes]);

// What an announce said, once its destination and signature have checked.
struct Announce {
  uint8_t destination[kHashBytes];
  uint8_t public_key[kPublicBytes];
  uint8_t name_hash[kNameHashBytes];
  int64_t emitted = 0;  // Its send time, seconds
  const uint8_t* enc_pub() const { return public_key; }
  const uint8_t* sig_pub() const { return public_key + 32; }
};

// An announce's destination and data field (kAnnounceMin + app_len bytes,
// at most kMtu - kHeaderMin); its length. `random` (5 bytes) and `now_s`
// for test vectors; else random bytes and `now_s` as given.
size_t build_announce(const Identity& identity,
                      const uint8_t name_hash[kNameHashBytes],
                      const uint8_t* app_data, size_t app_len, int64_t now_s,
                      uint8_t destination[kHashBytes], uint8_t* data,
                      const uint8_t* random = nullptr);

// Checks an announce packet: false when malformed, when its destination does
// not follow from its key and name, or when the signature fails.
bool parse_announce(const Packet& p, Announce* out);

}  // namespace rns
}  // namespace crewradio
