// SPDX-License-Identifier: EUPL-1.2
#include "rns_link.h"

#include <string.h>

#include "core/rns/curve25519.h"
#include "port/random.h"

namespace crewradio {
namespace rns {

namespace {
constexpr uint8_t kKeepaliveAsk = 0xFF;
constexpr uint8_t kKeepaliveAnswer = 0xFE;

void derive_key(const uint8_t shared[32], const uint8_t id[kHashBytes],
                uint8_t key[64]) {
  hkdf(key, 64, shared, 32, id, kHashBytes);
}
}  // namespace

void link_id_of(const uint8_t* raw, size_t len, size_t data_len,
                uint8_t out[kHashBytes]) {
  uint8_t full[32];
  hash_of(full, raw, len, data_len > 64 ? data_len - 64 : 0);
  memcpy(out, full, kHashBytes);
}

void pack_rtt(double seconds, uint8_t out[9]) {
  uint64_t bits;
  memcpy(&bits, &seconds, sizeof(bits));
  out[0] = 0xCB;
  for (int i = 0; i < 8; i++) out[1 + i] = (uint8_t)(bits >> (8 * (7 - i)));
}

Link::Link(const uint8_t id[kHashBytes], const uint8_t key[64], bool initiator)
    : initiator_(initiator), active_(initiator) {
  memcpy(id_, id, kHashBytes);
  memcpy(key_, key, 64);
}

Link::~Link() { memset(key_, 0, sizeof(key_)); }

size_t Link::data_packet(const uint8_t* plain, size_t len, uint8_t* out,
                         const uint8_t* iv) const {
  if (len > kMdu) return 0;
  uint8_t token[token_len(kMdu)];
  const size_t n = token_encrypt(key_, plain, len, token, iv);
  return encode(out, kData, kLink, id_, kCtxNone, token, n);
}

size_t Link::rtt_packet(double seconds, uint8_t* out) const {
  uint8_t rtt[9];
  pack_rtt(seconds, rtt);
  uint8_t token[token_len(sizeof(rtt))];
  const size_t n = token_encrypt(key_, rtt, sizeof(rtt), token);
  return encode(out, kData, kLink, id_, kCtxLrRtt, token, n);
}

size_t Link::close_packet(uint8_t* out) const {
  uint8_t token[token_len(kHashBytes)];
  const size_t n = token_encrypt(key_, id_, kHashBytes, token);
  return encode(out, kData, kLink, id_, kCtxLinkClose, token, n);
}

size_t Link::keepalive_packet(bool answer, uint8_t* out) const {
  const uint8_t b = answer ? kKeepaliveAnswer : kKeepaliveAsk;
  return encode(out, kData, kLink, id_, kCtxKeepalive, &b, 1);
}

Link::Event Link::handle(const Packet& p, uint8_t* plain, size_t* plain_len,
                         uint8_t* reply, size_t* reply_len) {
  if (p.packet_type != kData || closed_) return Event::kNone;
  if (p.context == kCtxKeepalive) {
    if (p.data_len != 1) return Event::kNone;
    *reply_len = p.data[0] == kKeepaliveAsk ? keepalive_packet(true, reply) : 0;
    return Event::kKeepalive;
  }
  // Nothing larger than a packet's worth decrypts here: room for the
  // plaintext and its padding.
  if (p.data_len > token_len(kMdu)) return Event::kNone;
  const int n = token_decrypt(key_, p.data, p.data_len, plain);
  if (n < 0) return Event::kNone;
  *plain_len = (size_t)n;
  switch (p.context) {
    case kCtxNone:
      active_ = true;
      return Event::kData;
    case kCtxLrRtt:
      active_ = true;
      return Event::kRtt;
    case kCtxLinkClose:
      if (*plain_len == kHashBytes && memcmp(plain, id_, kHashBytes) == 0) {
        closed_ = true;
        return Event::kClose;
      }
      return Event::kNone;
    default:
      return Event::kNone;
  }
}

bool Request::complete(const Packet& proof, Link* out) const {
  if (proof.packet_type != kProof || proof.context != kCtxLrProof ||
      memcmp(proof.destination, id, kHashBytes) != 0) {
    return false;
  }
  if (proof.data_len != 96 && proof.data_len != 99) return false;
  const uint8_t* signature = proof.data;
  const uint8_t* peer_enc_pub = proof.data + 64;
  const size_t signalling = proof.data_len - 96;
  uint8_t msg[kHashBytes + 32 + 32 + 3];
  memcpy(msg, id, kHashBytes);
  memcpy(msg + kHashBytes, peer_enc_pub, 32);
  memcpy(msg + kHashBytes + 32, peer_sig_pub, 32);
  if (signalling > 0) memcpy(msg + kHashBytes + 64, proof.data + 96, signalling);
  if (!curve25519::ed25519_verify(peer_sig_pub, msg, kHashBytes + 64 + signalling,
                                  signature)) {
    return false;
  }
  uint8_t shared[32], key[64];
  if (!agree(enc_priv, peer_enc_pub, shared)) return false;
  derive_key(shared, id, key);
  *out = Link(id, key, /*initiator=*/true);
  memset(shared, 0, sizeof(shared));
  memset(key, 0, sizeof(key));
  return true;
}

void request(const Announce& peer, const uint8_t* transport_id, Request* out,
             const uint8_t* enc_priv, const uint8_t* sig_seed) {
  uint8_t seed[32];
  if (enc_priv != nullptr) {
    memcpy(out->enc_priv, enc_priv, 32);
  } else {
    port::random_bytes(out->enc_priv, 32);
  }
  if (sig_seed != nullptr) {
    memcpy(seed, sig_seed, 32);
  } else {
    port::random_bytes(seed, 32);
  }
  uint8_t data[64];
  curve25519::x25519_public(data, out->enc_priv);
  curve25519::ed25519_public(data + 32, seed);
  memset(seed, 0, sizeof(seed));
  out->raw_len = encode(out->raw, kLinkRequest, kSingle, peer.destination,
                        kCtxNone, data, sizeof(data), transport_id);
  link_id_of(out->raw, out->raw_len, sizeof(data), out->id);
  memcpy(out->peer, peer.destination, kHashBytes);
  memcpy(out->peer_sig_pub, peer.sig_pub(), 32);
}

bool accept(const Identity& identity, const Packet& req, Link* link,
            uint8_t* proof, size_t* proof_len, const uint8_t* enc_priv) {
  if (req.packet_type != kLinkRequest ||
      (req.data_len != 64 && req.data_len != 67)) {
    return false;
  }
  uint8_t id[kHashBytes];
  link_id_of(req.raw, req.raw_len, req.data_len, id);
  uint8_t priv[32];
  if (enc_priv != nullptr) {
    memcpy(priv, enc_priv, 32);
  } else {
    port::random_bytes(priv, 32);
  }
  uint8_t shared[32];
  if (!agree(priv, req.data, shared)) {
    memset(priv, 0, sizeof(priv));
    return false;
  }
  uint8_t enc_pub[32];
  curve25519::x25519_public(enc_pub, priv);
  memset(priv, 0, sizeof(priv));
  uint8_t msg[kHashBytes + 64];
  memcpy(msg, id, kHashBytes);
  memcpy(msg + kHashBytes, enc_pub, 32);
  memcpy(msg + kHashBytes + 32, identity.sig_pub(), 32);
  uint8_t body[kSigBytes + 32];
  identity.sign(msg, sizeof(msg), body);
  memcpy(body + kSigBytes, enc_pub, 32);
  *proof_len = encode(proof, kProof, kLink, id, kCtxLrProof, body, sizeof(body));
  uint8_t key[64];
  derive_key(shared, id, key);
  *link = Link(id, key, /*initiator=*/false);
  memset(shared, 0, sizeof(shared));
  memset(key, 0, sizeof(key));
  return true;
}

}  // namespace rns
}  // namespace crewradio
