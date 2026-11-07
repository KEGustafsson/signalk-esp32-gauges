// SPDX-License-Identifier: EUPL-1.2
#include "rns_identity.h"

#include <string.h>

#include "core/rns/curve25519.h"
#include "port/random.h"

namespace crewradio {
namespace rns {

Identity::Identity(const uint8_t enc_priv[32], const uint8_t sig_seed[32]) {
  memcpy(enc_priv_, enc_priv, 32);
  memcpy(sig_seed_, sig_seed, 32);
  curve25519::x25519_public(public_key_, enc_priv_);
  curve25519::ed25519_public(public_key_ + 32, sig_seed_);
  ok_ = truncated_hash(hash_, public_key_, kPublicBytes);
}

Identity::~Identity() {
  memset(enc_priv_, 0, sizeof(enc_priv_));
  memset(sig_seed_, 0, sizeof(sig_seed_));
}

Identity Identity::generate() {
  uint8_t enc[32], sig[32];
  port::random_bytes(enc, sizeof(enc));
  port::random_bytes(sig, sizeof(sig));
  Identity id(enc, sig);
  memset(enc, 0, sizeof(enc));
  memset(sig, 0, sizeof(sig));
  return id;
}

void Identity::sign(const uint8_t* msg, size_t len,
                    uint8_t sig[kSigBytes]) const {
  curve25519::ed25519_sign(sig, sig_seed_, sig_pub(), msg, len);
}

bool Identity::agree(const uint8_t peer_pub[32], uint8_t out[32]) const {
  return rns::agree(enc_priv_, peer_pub, out);
}

bool agree(const uint8_t priv[32], const uint8_t peer_pub[32],
           uint8_t out[32]) {
  curve25519::x25519(out, priv, peer_pub);
  uint8_t any = 0;
  for (int i = 0; i < 32; i++) any |= out[i];
  return any != 0;
}

bool name_hash(const char* name, uint8_t out[kNameHashBytes]) {
  uint8_t full[32];
  const bool ok = sha256(full, (const uint8_t*)name, strlen(name));
  memcpy(out, full, kNameHashBytes);
  return ok;
}

bool destination_hash(const uint8_t nh[kNameHashBytes],
                      const uint8_t identity_hash[kHashBytes],
                      uint8_t out[kHashBytes]) {
  const Part parts[2] = {{nh, kNameHashBytes}, {identity_hash, kHashBytes}};
  return truncated_hash(out, parts, 2);
}

namespace {
// The signed part of an announce: destination | key | name | random |
// ratchet | app data, at most a packet's worth.
constexpr size_t kSignedMax = kHashBytes + kMtu;
}  // namespace

size_t build_announce(const Identity& identity,
                      const uint8_t nh[kNameHashBytes],
                      const uint8_t* app_data, size_t app_len, int64_t now_s,
                      uint8_t destination[kHashBytes], uint8_t* data,
                      const uint8_t* random) {
  if (kAnnounceMin + app_len > kMtu - kHeaderMin) return 0;
  uint8_t random_hash[kRandomHashBytes];
  if (random != nullptr) {
    memcpy(random_hash, random, 5);
  } else {
    port::random_bytes(random_hash, 5);
  }
  const uint64_t t = (uint64_t)now_s & ((1ULL << 40) - 1);
  for (int i = 0; i < 5; i++) random_hash[5 + i] = (uint8_t)(t >> (8 * (4 - i)));
  destination_hash(nh, identity.hash(), destination);

  uint8_t msg[kSignedMax];
  size_t n = 0;
  memcpy(msg + n, destination, kHashBytes);
  n += kHashBytes;
  memcpy(msg + n, identity.public_key(), kPublicBytes);
  n += kPublicBytes;
  memcpy(msg + n, nh, kNameHashBytes);
  n += kNameHashBytes;
  memcpy(msg + n, random_hash, kRandomHashBytes);
  n += kRandomHashBytes;
  if (app_len > 0) memcpy(msg + n, app_data, app_len);
  n += app_len;
  uint8_t sig[kSigBytes];
  identity.sign(msg, n, sig);

  size_t at = 0;
  memcpy(data + at, identity.public_key(), kPublicBytes);
  at += kPublicBytes;
  memcpy(data + at, nh, kNameHashBytes);
  at += kNameHashBytes;
  memcpy(data + at, random_hash, kRandomHashBytes);
  at += kRandomHashBytes;
  memcpy(data + at, sig, kSigBytes);
  at += kSigBytes;
  if (app_len > 0) memcpy(data + at, app_data, app_len);
  return at + app_len;
}

bool parse_announce(const Packet& p, Announce* out) {
  const uint8_t* d = p.data;
  const size_t ratchet_len = p.context_flag ? kRatchetBytes : 0;
  if (p.data_len < kAnnounceMin + ratchet_len) return false;
  const size_t name_at = kPublicBytes;
  const size_t random_at = name_at + kNameHashBytes;
  const size_t ratchet_at = random_at + kRandomHashBytes;
  const size_t sig_at = ratchet_at + ratchet_len;
  const size_t app_at = sig_at + kSigBytes;
  const size_t app_len = p.data_len - app_at;

  uint8_t id_hash[kHashBytes], dest[kHashBytes];
  truncated_hash(id_hash, d, kPublicBytes);
  destination_hash(d + name_at, id_hash, dest);
  if (memcmp(dest, p.destination, kHashBytes) != 0) return false;

  uint8_t msg[kSignedMax];
  size_t n = 0;
  memcpy(msg, p.destination, kHashBytes);
  n += kHashBytes;
  memcpy(msg + n, d, sig_at);  // Key, name, random hash, ratchet
  n += sig_at;
  if (app_len > 0) memcpy(msg + n, d + app_at, app_len);
  n += app_len;
  if (!curve25519::ed25519_verify(d + 32, msg, n, d + sig_at)) return false;

  memcpy(out->destination, p.destination, kHashBytes);
  memcpy(out->public_key, d, kPublicBytes);
  memcpy(out->name_hash, d + name_at, kNameHashBytes);
  int64_t emitted = 0;
  for (size_t i = 5; i < 10; i++) emitted = (emitted << 8) | d[random_at + i];
  out->emitted = emitted;
  return true;
}

}  // namespace rns
}  // namespace crewradio
