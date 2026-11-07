// SPDX-License-Identifier: EUPL-1.2
#include "carry.h"

#include <string.h>

namespace crewradio {
namespace rns {
namespace carry {

namespace {
constexpr uint8_t kWhole = 1;
constexpr uint8_t kKeyProof = 0x80;
constexpr uint8_t kAskRequest = 0x81;
constexpr uint8_t kAskAnswer = 0x82;
constexpr size_t kAskHead = 5;  // kind | id u16 | index | count
}  // namespace

bool key_proof(const uint8_t confirm_key[32], const uint8_t link_id[kHashBytes],
               bool initiator, uint8_t out[kProofBytes]) {
  const uint8_t role = initiator ? 1 : 0;
  const Part parts[2] = {{&role, 1}, {link_id, kHashBytes}};
  out[0] = kKeyProof;
  return hmac_sha256(out + 1, confirm_key, 32, parts, 2);
}

bool is_key_proof(const uint8_t* payload, size_t len) {
  return len == kProofBytes && payload[0] == kKeyProof;
}

bool proof_matches(const uint8_t* payload, size_t len,
                   const uint8_t confirm_key[32],
                   const uint8_t link_id[kHashBytes], bool initiator) {
  if (!is_key_proof(payload, len)) return false;
  uint8_t expected[kProofBytes];
  if (!key_proof(confirm_key, link_id, initiator, expected)) return false;
  return equal(payload, expected, kProofBytes);
}

bool is_ask(const uint8_t* payload, size_t len) {
  return len > kAskHead &&
         (payload[0] == kAskRequest || payload[0] == kAskAnswer);
}

size_t cut(const uint8_t* packet, size_t len, uint8_t id,
           uint8_t parts[kMaxParts][kMdu], size_t lens[kMaxParts]) {
  if (len + 1 <= kMdu) {
    parts[0][0] = kWhole;
    memcpy(parts[0] + 1, packet, len);
    lens[0] = len + 1;
    return 1;
  }
  const size_t count = (len + kRoom - 1) / kRoom;
  if (count > kMaxParts) return 0;
  for (size_t i = 0; i < count; i++) {
    const size_t from = i * kRoom;
    const size_t n = len - from < kRoom ? len - from : kRoom;
    parts[i][0] = (uint8_t)count;
    parts[i][1] = (uint8_t)i;
    parts[i][2] = id;
    memcpy(parts[i] + kPartHead, packet + from, n);
    lens[i] = kPartHead + n;
  }
  return count;
}

bool Joiner::push(const uint8_t* payload, size_t len, const uint8_t** out,
                  size_t* out_len) {
  if (len < 2) return false;
  const int n = payload[0];
  if (n == kWhole) {
    *out = payload + 1;
    *out_len = len - 1;
    return true;
  }
  if (n < 2 || n > (int)kMaxParts || len <= kPartHead || len - kPartHead > kRoom) {
    reset();
    return false;
  }
  const int index = payload[1];
  const int pid = payload[2];
  const size_t body = len - kPartHead;
  if (index == 0) {
    reset();
    count_ = n;
    id_ = pid;
    next_ = 1;
    memcpy(buf_, payload + kPartHead, body);
    len_ = body;
    return false;
  }
  if (next_ == 0 || pid != id_ || n != count_ || index != next_) {
    reset();
    return false;
  }
  memcpy(buf_ + len_, payload + kPartHead, body);
  len_ += body;
  next_++;
  if (next_ < count_) return false;
  *out = buf_;  // The bytes stay in buf_ until the next push
  *out_len = len_;
  reset();
  return true;
}

}  // namespace carry
}  // namespace rns
}  // namespace crewradio
