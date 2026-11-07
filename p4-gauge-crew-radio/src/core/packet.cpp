// SPDX-License-Identifier: EUPL-1.2
#include "packet.h"

#include <string.h>

namespace crewradio {
namespace packet {

namespace {

uint8_t clamp_byte(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

void put32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)v;
}

uint32_t get32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

}  // namespace

void encode_header(const Header& h, uint8_t out[kHeader]) {
  out[0] = 'P';
  out[1] = 'T';
  out[2] = kVersion;
  out[3] = (uint8_t)h.codec;
  out[4] = clamp_byte(h.ttl);
  out[5] = clamp_byte(h.hops);
  put32(out + 6, (uint32_t)h.sender_id);
  put32(out + 10, (uint32_t)h.seq);
  put32(out + 14, h.time);
}

bool parse(const uint8_t* p, size_t len, Header* out) {
  if (p == nullptr || len <= kHeader || len > kMaxSize || p[0] != 'P' ||
      p[1] != 'T' || p[2] != kVersion) {
    return false;
  }
  if (p[3] > (uint8_t)Codec::kHello) return false;
  out->codec = (Codec)p[3];
  out->ttl = p[4];
  out->hops = p[5];
  out->sender_id = (int32_t)get32(p + 6);
  out->seq = (int32_t)get32(p + 10);
  out->time = get32(p + 14);
  return true;
}

bool is_fresh(uint32_t time, int64_t now_s) {
  const int32_t d = (int32_t)(time - (uint32_t)now_s);  // wraps with the counter
  return d >= -kReplayWindowS && d <= kReplayWindowS;
}

void aad_of(const uint8_t* p, uint8_t out[kHeader]) {
  memcpy(out, p, kHeader);
  out[4] = 0;
}

void set_ttl(uint8_t* p, int ttl) { p[4] = clamp_byte(ttl); }

}  // namespace packet
}  // namespace crewradio
