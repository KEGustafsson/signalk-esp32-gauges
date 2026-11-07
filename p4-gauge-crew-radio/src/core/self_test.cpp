// SPDX-License-Identifier: EUPL-1.2
#include "self_test.h"

#include <string.h>

#include "channel_crypto.h"
#include "hello.h"
#include "packet.h"

namespace crewradio {
namespace self_test {

// The crew-radio app's cross-language vector (tests/vectors/vector.json):
// the packet key derived from "north-star-2026" and a hello sealed under it.
// tests/test_self_test.cpp checks these bytes against the file.
const uint8_t kKey[32] = {
    0xc7, 0x4b, 0x23, 0x57, 0x83, 0xad, 0x92, 0x07, 0x54, 0x01, 0xd7, 0xb0,
    0x4e, 0x84, 0xf2, 0xd1, 0x38, 0xf4, 0xbf, 0x1e, 0x18, 0x46, 0x8b, 0x31,
    0x7c, 0xc4, 0xd7, 0x02, 0x7b, 0x19, 0xca, 0xf7,
};
const uint8_t kPacket[] = {
    0x50, 0x54, 0x04, 0x02, 0x04, 0x04, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00,
    0x00, 0x07, 0x6a, 0x9d, 0xfe, 0x80, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x00, 0x62, 0x2f, 0x48, 0x4f, 0x4a,
    0x8d, 0xe7, 0x23, 0x10, 0x92, 0x50, 0xe7, 0x78, 0xf7, 0x88, 0xe7, 0xf5,
    0x0b, 0xeb, 0x46, 0x18, 0x34, 0x13, 0x0f, 0x40, 0x4b, 0x53,
};
const size_t kPacketLen = sizeof(kPacket);
const uint8_t kPlain[] = {
    0x02, 0x01, 0x04, 0x00, 0x86, 0x06, 0x53, 0x69, 0x72, 0x69, 0x75, 0x73,
};
const size_t kPlainLen = sizeof(kPlain);

bool run() {
  Header h;
  if (!packet::parse(kPacket, kPacketLen, &h)) return false;
  if (h.codec != Codec::kHello || h.sender_id != 305419896 || h.seq != 7 ||
      h.time != 1788739200u || h.ttl != 4 || h.hops != 4) {
    return false;
  }
  uint8_t aad[packet::kHeader];
  packet::aad_of(kPacket, aad);
  ChannelCrypto crypto(kKey);
  uint8_t plain[sizeof(kPacket)];
  const int n = crypto.open(aad, sizeof(aad), kPacket + packet::kHeader,
                            kPacketLen - packet::kHeader, plain);
  if (n != (int)kPlainLen || memcmp(plain, kPlain, kPlainLen) != 0) return false;
  Hello hello;
  if (!Hello::decode(plain, (size_t)n, &hello)) return false;
  if (hello.name != "Sirius" || hello.transports != 1 || hello.ttl != 4 ||
      hello.version_code != 134) {
    return false;
  }
  // And the other way: sealing the plaintext again under the vector's nonce
  // gives the packet, byte for byte.
  uint8_t again[sizeof(kPacket)];
  memcpy(again, kPacket, packet::kHeader);
  if (!crypto.seal_with_nonce(aad, sizeof(aad), kPlain, kPlainLen,
                              kPacket + packet::kHeader, again + packet::kHeader)) {
    return false;
  }
  return memcmp(again, kPacket, kPacketLen) == 0;
}

}  // namespace self_test
}  // namespace crewradio
