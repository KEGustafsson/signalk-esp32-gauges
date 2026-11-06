// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Crew Radio packets inside a Reticulum link, as the app's Carry (in
// RnsLink.kt) and the plugin's lib/rns/carry.js: the sealed packet unchanged
// behind one byte. An Opus frame or a hello fits whole; a PCM frame (686
// bytes sealed) goes in parts, put back together only in order, so a lost
// part drops that one 20 ms frame and nothing after it:
//
//   whole: 0x01 | packet           part: count (2-3) | index | id u8 | bytes
//   proof: 0x80 | HMAC-SHA256(confirm key, role | link id)
//          (role 1 = the end that dialled)
//
// 0x81 and 0x82 lead the parts of a question for the boat and its answer
// (the app's AskCarry): this node neither asks nor answers, and drops them.
//
// The proof is how a link is confirmed: each end sends its own as soon as
// the link is up, and nothing else goes either way until the far end's has
// checked out. It is bound to the link id (fresh keys on every link) and to
// the sender's role, so a proof seen on one link is worthless on any other
// and cannot be echoed back to its maker; a sealed channel packet, which
// anyone can copy from anywhere, proves nothing about the link it came on.

#include <stddef.h>
#include <stdint.h>

#include "core/rns/rns_link.h"

namespace crewradio {
namespace rns {
namespace carry {

constexpr size_t kMaxParts = 3;
constexpr size_t kPartHead = 3;
constexpr size_t kRoom = kMdu - kPartHead;  // A part's bytes
constexpr size_t kMaxPacket = kMaxParts * kRoom;
constexpr size_t kProofBytes = 1 + 32;

// The key proof the end in `initiator`'s role sends on link_id.
void key_proof(const uint8_t confirm_key[32], const uint8_t link_id[kHashBytes],
               bool initiator, uint8_t out[kProofBytes]);
// True when payload is a key proof frame, whatever it proves.
bool is_key_proof(const uint8_t* payload, size_t len);
// True when payload is the far end's proof for this link, made in
// `initiator`'s role (compared in constant time).
bool proof_matches(const uint8_t* payload, size_t len,
                   const uint8_t confirm_key[32],
                   const uint8_t link_id[kHashBytes], bool initiator);
// A question for the boat or its answer (the app's AskCarry.isAsk).
bool is_ask(const uint8_t* payload, size_t len);

// The parts for `packet` (each at most kMdu bytes, into parts[i] with
// lens[i]): how many, or 0 when it is too large to carry.
size_t cut(const uint8_t* packet, size_t len, uint8_t id,
           uint8_t parts[kMaxParts][kMdu], size_t lens[kMaxParts]);

// One link's reassembly.
class Joiner {
 public:
  // A whole packet: true with *out and *out_len (a view into payload or
  // into this joiner, valid until the next push).
  bool push(const uint8_t* payload, size_t len, const uint8_t** out,
            size_t* out_len);

 private:
  void reset() {
    count_ = 0;
    id_ = -1;
    next_ = 0;
    len_ = 0;
  }
  int count_ = 0;
  int id_ = -1;
  int next_ = 0;
  size_t len_ = 0;
  uint8_t buf_[kMaxPacket];
};

}  // namespace carry
}  // namespace rns
}  // namespace crewradio
