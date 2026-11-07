// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Reticulum packets and the HDLC framing of its TCP interfaces, as the app's
// RnsPacket.kt (from the Reticulum manual's "Wire Format"):
//
//   flags u8 | hops u8 | [transport id (16) when header type 2] |
//   destination (16) | context u8 | data
//
//   flags = ifac(1 bit) | header type(1) | context flag(1) | propagation(1) |
//           destination type(2) | packet type(2)
//
// Interface access codes are not supported: a packet with the IFAC bit set
// is not parsed. On TCP each packet is a frame: 0x7E, the packet with 0x7D
// and 0x7E escaped as 0x7D and the byte XOR 0x20, 0x7E.

#include <stddef.h>
#include <stdint.h>

#include "core/rns/rns_crypto.h"

namespace crewradio {
namespace rns {

constexpr size_t kMtu = 500;
constexpr size_t kHeaderMin = 2 + kHashBytes + 1;
// A framed packet: every byte escaped, and the two flags.
constexpr size_t kMaxFrame = 2 * kMtu + 2;

enum HeaderType : uint8_t { kHeader1 = 0, kHeader2 = 1 };
enum DestType : uint8_t { kSingle = 0, kGroup = 1, kPlain = 2, kLink = 3 };
enum PacketType : uint8_t {
  kData = 0,
  kAnnounce = 1,
  kLinkRequest = 2,
  kProof = 3,
};
enum Context : uint8_t {
  kCtxNone = 0x00,
  kCtxKeepalive = 0xFA,
  kCtxLinkClose = 0xFC,
  kCtxLrRtt = 0xFE,
  kCtxLrProof = 0xFF,
};

// A parsed packet: views into the bytes it was parsed from.
struct Packet {
  uint8_t header_type = kHeader1;
  bool context_flag = false;
  uint8_t packet_type = kData;
  uint8_t dest_type = kSingle;
  uint8_t hops = 0;
  const uint8_t* transport_id = nullptr;  // Header type 2 only
  const uint8_t* destination = nullptr;   // kHashBytes
  uint8_t context = kCtxNone;
  const uint8_t* data = nullptr;
  size_t data_len = 0;
  const uint8_t* raw = nullptr;
  size_t raw_len = 0;
};

// Builds a packet into out (room for kMtu, or for the packet): its length,
// or 0 when it would not fit. A transport_id makes it header type 2 with transport propagation,
// addressed through that next hop.
size_t encode(uint8_t* out, uint8_t packet_type, uint8_t dest_type,
              const uint8_t destination[kHashBytes], uint8_t context,
              const uint8_t* data, size_t data_len,
              const uint8_t* transport_id = nullptr,
              bool context_flag = false, uint8_t hops = 0);

// False for anything too short, too long or carrying an interface access
// code.
bool decode(const uint8_t* raw, size_t len, Packet* out);

// The packet's hash: SHA-256 over the low nibble of the flags and everything
// after the hops and any transport id, so the same however it travelled.
// `drop_tail` bytes are left off the end (a link request's signalling).
void hash_of(uint8_t out[32], const uint8_t* raw, size_t len,
             size_t drop_tail = 0);

// The frame for `packet` into out (kMaxFrame bytes): its length.
size_t frame(const uint8_t* packet, size_t len, uint8_t* out);

// Collects frames from a byte stream. Bytes before the first flag are
// dropped; a frame longer than kMax is abandoned up to the next flag, so a
// peer that never sends one cannot grow it.
class Deframer {
 public:
  static constexpr size_t kMax = kMtu + 64;

  template <typename OnFrame>
  void push(const uint8_t* chunk, size_t len, OnFrame&& on_frame) {
    for (size_t k = 0; k < len; k++) {
      const uint8_t b = chunk[k];
      if (b == kFlag) {
        if (in_frame_ && len_ > 0 && !overflow_) on_frame(buf_, len_);
        len_ = 0;
        in_frame_ = true;
        escape_ = false;
        overflow_ = false;
      } else if (!in_frame_ || overflow_) {
        // Outside a frame, or past the limit: skipped
      } else if (escape_) {
        put((uint8_t)(b ^ kEscMask));
        escape_ = false;
      } else if (b == kEsc) {
        escape_ = true;
      } else {
        put(b);
      }
    }
  }

  static constexpr uint8_t kFlag = 0x7E;
  static constexpr uint8_t kEsc = 0x7D;
  static constexpr uint8_t kEscMask = 0x20;

 private:
  void put(uint8_t b) {
    if (len_ >= kMax) {
      len_ = 0;
      overflow_ = true;
      return;
    }
    buf_[len_++] = b;
  }

  uint8_t buf_[kMax];
  size_t len_ = 0;
  bool in_frame_ = false;
  bool escape_ = false;
  bool overflow_ = false;
};

}  // namespace rns
}  // namespace crewradio
