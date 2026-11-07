// SPDX-License-Identifier: EUPL-1.2
#include "rns_packet.h"

#include <string.h>

namespace crewradio {
namespace rns {

size_t encode(uint8_t* out, uint8_t packet_type, uint8_t dest_type,
              const uint8_t destination[kHashBytes], uint8_t context,
              const uint8_t* data, size_t data_len,
              const uint8_t* transport_id, bool context_flag, uint8_t hops) {
  const bool two = transport_id != nullptr;
  const size_t len = 2 + (two ? kHashBytes : 0) + kHashBytes + 1 + data_len;
  if (len > kMtu) return 0;
  out[0] = (uint8_t)(((two ? 1 : 0) << 6) | ((context_flag ? 1 : 0) << 5) |
                     ((two ? 1 : 0) << 4) | ((dest_type & 3) << 2) |
                     (packet_type & 3));
  out[1] = hops;
  size_t at = 2;
  if (two) {
    memcpy(out + at, transport_id, kHashBytes);
    at += kHashBytes;
  }
  memcpy(out + at, destination, kHashBytes);
  at += kHashBytes;
  out[at++] = context;
  if (data_len > 0) memcpy(out + at, data, data_len);
  return len;
}

bool decode(const uint8_t* raw, size_t len, Packet* out) {
  if (len < kHeaderMin || len > kMtu) return false;
  const uint8_t flags = raw[0];
  if ((flags & 0x80) != 0) return false;
  const uint8_t header_type = (flags >> 6) & 1;
  const size_t off = header_type == kHeader2 ? 2 + kHashBytes : 2;
  if (len < off + kHashBytes + 1) return false;
  out->header_type = header_type;
  out->context_flag = ((flags >> 5) & 1) == 1;
  out->packet_type = flags & 3;
  out->dest_type = (flags >> 2) & 3;
  out->hops = raw[1];
  out->transport_id = header_type == kHeader2 ? raw + 2 : nullptr;
  out->destination = raw + off;
  out->context = raw[off + kHashBytes];
  out->data = raw + off + kHashBytes + 1;
  out->data_len = len - (off + kHashBytes + 1);
  out->raw = raw;
  out->raw_len = len;
  return true;
}

void hash_of(uint8_t out[32], const uint8_t* raw, size_t len,
             size_t drop_tail) {
  const bool two = ((raw[0] >> 6) & 1) == 1;
  const size_t from = two ? 2 + kHashBytes : 2;
  const uint8_t nibble = raw[0] & 0x0F;
  const size_t rest = len - from > drop_tail ? len - from - drop_tail : 0;
  const Part parts[2] = {{&nibble, 1}, {raw + from, rest}};
  sha256(out, parts, 2);
}

size_t frame(const uint8_t* packet, size_t len, uint8_t* out) {
  size_t n = 0;
  out[n++] = Deframer::kFlag;
  for (size_t i = 0; i < len; i++) {
    const uint8_t v = packet[i];
    if (v == Deframer::kFlag || v == Deframer::kEsc) {
      out[n++] = Deframer::kEsc;
      out[n++] = (uint8_t)(v ^ Deframer::kEscMask);
    } else {
      out[n++] = v;
    }
  }
  out[n++] = Deframer::kFlag;
  return n;
}

}  // namespace rns
}  // namespace crewradio
