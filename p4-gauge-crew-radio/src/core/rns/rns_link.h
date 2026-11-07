// SPDX-License-Identifier: EUPL-1.2
#pragma once

// A Reticulum link, either end, as the app's RnsLink.kt (the manual's "Link
// Establishment in Detail"):
//
//  - request (initiator to the destination): ephemeral X25519 public key |
//    ephemeral Ed25519 public key. The link id is the truncated hash of the
//    request's hashable part.
//  - proof (destination to the link id, context LRPROOF): signature | the
//    responder's ephemeral X25519 public key, signed by the destination's
//    identity over link id | that key | the identity's Ed25519 public key.
//  - rtt (initiator to the link id): the round trip as a MessagePack float,
//    encrypted; it tells the responder the link is up.
//
// Both ends hold HKDF-SHA256(ECDH secret, salt = link id) as a 64-byte
// token key; every data packet is one token. Keep-alives (context
// KEEPALIVE, one clear byte: 0xFF asks, 0xFE answers) and the close
// (context LINKCLOSE, the link id as a token) complete it. No MTU signalling
// is sent, so a link runs at the base MTU: kMdu bytes of plaintext a packet.

#include <stddef.h>
#include <stdint.h>

#include "core/rns/rns_identity.h"

namespace crewradio {
namespace rns {

// Plaintext bytes a link packet carries at the base MTU: whole AES blocks,
// less the byte PKCS#7 always adds.
constexpr size_t kMdu =
    ((kMtu - 1 - kHeaderMin - kTokenOverhead) / 16) * 16 - 1;
static_assert(kMdu == 431, "the app's and the plugin's link MDU");

// The link id of a request: its hashable part, less any signalling bytes
// past the two keys.
void link_id_of(const uint8_t* raw, size_t len, size_t data_len,
                uint8_t out[kHashBytes]);
// MessagePack float 64, as the RTT packet carries it (9 bytes).
void pack_rtt(double seconds, uint8_t out[9]);

class Link {
 public:
  enum class Event : uint8_t { kNone, kData, kRtt, kClose, kKeepalive };

  Link() = default;
  Link(const uint8_t id[kHashBytes], const uint8_t key[64], bool initiator);
  ~Link();

  const uint8_t* id() const { return id_; }
  bool initiator() const { return initiator_; }
  bool active() const { return active_; }
  bool closed() const { return closed_; }
  // The token key, for the cross-language test.
  const uint8_t* key_for_test() const { return key_; }

  // Each writes a whole packet into out (kMtu bytes) and returns its length;
  // 0 for a payload over kMdu.
  size_t data_packet(const uint8_t* plain, size_t len, uint8_t* out,
                     const uint8_t* iv = nullptr) const;
  size_t rtt_packet(double seconds, uint8_t* out) const;
  size_t close_packet(uint8_t* out) const;
  size_t keepalive_packet(bool answer, uint8_t* out) const;

  // What a packet addressed to this link meant: kNone for anything that does
  // not authenticate or that these links do not use. For kData the
  // plaintext is in plain (kMdu + 16 bytes of room) with its length; for a
  // keep-alive that asks, the answer is in reply (kMtu) with its length.
  Event handle(const Packet& p, uint8_t* plain, size_t* plain_len,
               uint8_t* reply, size_t* reply_len);

 private:
  uint8_t id_[kHashBytes] = {};
  uint8_t key_[64] = {};
  bool initiator_ = false;
  bool active_ = false;
  bool closed_ = false;
};

// A request on its way. `peer` is the announced destination it goes to.
struct Request {
  uint8_t raw[kHeaderMin + kHashBytes + 64];
  size_t raw_len = 0;
  uint8_t id[kHashBytes];
  uint8_t peer[kHashBytes];
  uint8_t peer_sig_pub[32];
  uint8_t enc_priv[32];

  // Checks the proof against the destination's key: true with *out the
  // link.
  bool complete(const Packet& proof, Link* out) const;
};

// Starts a link to an announced destination; transport_id names the next
// hop when it is further than one. Keys only for tests (else random).
void request(const Announce& peer, const uint8_t* transport_id, Request* out,
             const uint8_t* enc_priv = nullptr,
             const uint8_t* sig_seed = nullptr);

// Answers a request to identity's destination: true with the link (active
// once the initiator's first packet arrives) and the proof to send (kMtu
// bytes of room). `enc_priv` only for tests.
bool accept(const Identity& identity, const Packet& request, Link* link,
            uint8_t* proof, size_t* proof_len,
            const uint8_t* enc_priv = nullptr);

}  // namespace rns
}  // namespace crewradio
