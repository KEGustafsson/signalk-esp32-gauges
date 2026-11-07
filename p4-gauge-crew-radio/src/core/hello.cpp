// SPDX-License-Identifier: EUPL-1.2
#include "hello.h"

#include <string.h>

namespace crewradio {

namespace {

// Unicode general category Cf (format), Unicode 15.1. Java's
// Character.getType(cp) == FORMAT and JavaScript's \p{Cf} use the same
// table, give or take the Unicode version of the runtime.
struct Range {
  uint32_t lo, hi;
};
constexpr Range kFormat[] = {
    {0x00AD, 0x00AD},   {0x0600, 0x0605},   {0x061C, 0x061C},
    {0x06DD, 0x06DD},   {0x070F, 0x070F},   {0x0890, 0x0891},
    {0x08E2, 0x08E2},   {0x180E, 0x180E},   {0x200B, 0x200F},
    {0x202A, 0x202E},   {0x2060, 0x2064},   {0x2066, 0x206F},
    {0xFEFF, 0xFEFF},   {0xFFF9, 0xFFFB},   {0x110BD, 0x110BD},
    {0x110CD, 0x110CD}, {0x13430, 0x1343F}, {0x1BCA0, 0x1BCA3},
    {0x1D173, 0x1D17A}, {0xE0001, 0xE0001}, {0xE0020, 0xE007F},
};

bool is_control(uint32_t cp) { return cp <= 0x1F || (cp >= 0x7F && cp <= 0x9F); }

bool is_format(uint32_t cp) {
  for (const Range& r : kFormat) {
    if (cp >= r.lo && cp <= r.hi) return true;
  }
  return false;
}

// Java's isWhitespace || isSpaceChar, past the controls (dropped first):
// Zs, Zl and Zp. The same set as JavaScript's \s once Cc and Cf are gone.
bool is_space(uint32_t cp) {
  return cp == 0x20 || cp == 0xA0 || cp == 0x1680 ||
         (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
         cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

// Length of the UTF-8 sequence at p (1..4), or 0 if it is not well formed.
size_t sequence_len(const uint8_t* p, size_t left, uint32_t* cp) {
  const uint8_t c = p[0];
  size_t n;
  uint32_t v, min;
  if (c < 0x80) {
    *cp = c;
    return 1;
  } else if ((c & 0xE0) == 0xC0) {
    n = 2, v = c & 0x1F, min = 0x80;
  } else if ((c & 0xF0) == 0xE0) {
    n = 3, v = c & 0x0F, min = 0x800;
  } else if ((c & 0xF8) == 0xF0) {
    n = 4, v = c & 0x07, min = 0x10000;
  } else {
    return 0;
  }
  if (left < n) return 0;
  for (size_t i = 1; i < n; i++) {
    if ((p[i] & 0xC0) != 0x80) return 0;
    v = (v << 6) | (p[i] & 0x3F);
  }
  if (v < min || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF)) return 0;
  *cp = v;
  return n;
}

void append_utf8(std::string* s, uint32_t cp) {
  if (cp < 0x80) {
    s->push_back((char)cp);
  } else if (cp < 0x800) {
    s->push_back((char)(0xC0 | (cp >> 6)));
    s->push_back((char)(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    s->push_back((char)(0xE0 | (cp >> 12)));
    s->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    s->push_back((char)(0x80 | (cp & 0x3F)));
  } else {
    s->push_back((char)(0xF0 | (cp >> 18)));
    s->push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
    s->push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
    s->push_back((char)(0x80 | (cp & 0x3F)));
  }
}

}  // namespace

bool valid_utf8(const uint8_t* p, size_t len) {
  for (size_t i = 0; i < len;) {
    uint32_t cp;
    const size_t n = sequence_len(p + i, len - i, &cp);
    if (n == 0) return false;
    i += n;
  }
  return true;
}

size_t utf8_prefix_len(const std::string& utf8, size_t max_bytes) {
  const uint8_t* p = (const uint8_t*)utf8.data();
  size_t end = 0;
  while (end < utf8.size()) {
    uint32_t cp;
    size_t n = sequence_len(p + end, utf8.size() - end, &cp);
    if (n == 0) break;  // Not UTF-8 from here on: keep what came before
    if (end + n > max_bytes) break;
    end += n;
  }
  return end;
}

std::string sanitise_name(const std::string& utf8) {
  std::string out;
  out.reserve(utf8.size());
  const uint8_t* p = (const uint8_t*)utf8.data();
  bool space = false;
  for (size_t i = 0; i < utf8.size();) {
    uint32_t cp;
    const size_t n = sequence_len(p + i, utf8.size() - i, &cp);
    if (n == 0) {  // Callers pass valid UTF-8; skip a stray byte anyway
      i++;
      continue;
    }
    i += n;
    if (is_control(cp) || is_format(cp)) continue;
    if (is_space(cp)) {
      space = true;
      continue;
    }
    if (space && !out.empty()) out.push_back(' ');
    space = false;
    append_utf8(&out, cp);
  }
  return out;
}

size_t Hello::encode(uint8_t* out) const {
  const size_t name_len = utf8_prefix_len(name, kMaxNameBytes);
  out[0] = kVersion;
  out[1] = transports;
  out[2] = (uint8_t)(ttl < 0 ? 0 : ttl > 255 ? 255 : ttl);
  out[3] = (uint8_t)(version_code >> 8);
  out[4] = (uint8_t)version_code;
  out[5] = (uint8_t)name_len;
  memcpy(out + kHead, name.data(), name_len);
  return kHead + name_len;
}

bool Hello::decode(const uint8_t* p, size_t len, Hello* out) {
  if (p == nullptr || len < kHead || p[0] != kVersion) return false;
  const size_t name_len = p[5];
  if (name_len > kMaxNameBytes || len != kHead + name_len) return false;
  if (!valid_utf8(p + kHead, name_len)) return false;
  out->name = sanitise_name(std::string((const char*)p + kHead, name_len));
  out->transports = p[1];
  out->ttl = p[2];
  out->version_code = (uint16_t)((p[3] << 8) | p[4]);
  return true;
}

}  // namespace crewradio
