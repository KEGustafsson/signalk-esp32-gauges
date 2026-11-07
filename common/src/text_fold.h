// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Text the gauges' fonts can draw. LVGL's built-in Montserrat carries ASCII
// (0x20-0x7E) and nothing else: a letter outside it is left out, so a crew
// member called "Mäkelä" would show as "Mkel". ascii_fold() spells such text
// in ASCII instead: Latin letters with accents and strokes as their base
// letters (ä a, ø o, ß ss, Æ AE, ł l), typographic quotes, dashes and the
// ellipsis as their plain forms, a no-break space as a space, anything else
// (another script, an emoji, malformed UTF-8) as '?'. Control characters go.
// Pure and header-only, host-tested (tests/test_text_fold.cpp).

#include <stdint.h>

#include <string>

namespace skgauges {

namespace text_fold_detail {

// U+00C0..U+017F (Latin-1 letters and Latin Extended-A), from Unicode's
// decompositions; the letters without one (Æ ß Ø Ł Đ Þ ...) by convention.
inline const char* latin(uint32_t cp) {
  static const char* const kTable[0x180 - 0xC0] = {
      "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E",
      "I", "I", "I", "I", "D", "N", "O", "O", "O", "O", "O", "x",
      "O", "U", "U", "U", "U", "Y", "Th", "ss", "a", "a", "a", "a",
      "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
      "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u",
      "u", "y", "th", "y", "A", "a", "A", "a", "A", "a", "C", "c",
      "C", "c", "C", "c", "C", "c", "D", "d", "D", "d", "E", "e",
      "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g",
      "G", "g", "G", "g", "H", "h", "H", "h", "I", "i", "I", "i",
      "I", "i", "I", "i", "I", "i", "IJ", "ij", "J", "j", "K", "k",
      "k", "L", "l", "L", "l", "L", "l", "L", "l", "L", "l", "N",
      "n", "N", "n", "N", "n", "'n", "N", "n", "O", "o", "O", "o",
      "O", "o", "OE", "oe", "R", "r", "R", "r", "R", "r", "S", "s",
      "S", "s", "S", "s", "S", "s", "T", "t", "T", "t", "T", "t",
      "U", "u", "U", "u", "U", "u", "U", "u", "U", "u", "U", "u",
      "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s",
  };
  return kTable[cp - 0xC0];
}

inline const char* fold(uint32_t cp) {
  if (cp >= 0xC0 && cp < 0x180) return latin(cp);
  switch (cp) {
    case 0xA0: return " ";                      // No-break space
    case 0x2018: case 0x2019: case 0x201A: return "'";
    case 0x201C: case 0x201D: case 0x201E: return "\"";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014:
    case 0x2212: return "-";
    case 0x2026: return "...";
    case 0x00B4: return "'";
    default: return "?";
  }
}

}  // namespace text_fold_detail

inline std::string ascii_fold(const std::string& utf8) {
  std::string out;
  out.reserve(utf8.size());
  const size_t n = utf8.size();
  size_t i = 0;
  while (i < n) {
    const uint8_t b = (uint8_t)utf8[i];
    if (b < 0x80) {
      if (b >= 0x20 && b < 0x7F) out += (char)b;  // Controls and DEL go
      i++;
      continue;
    }
    // The length a lead byte announces, and its payload bits.
    int len = 0;
    uint32_t cp = 0;
    if ((b & 0xE0) == 0xC0) {
      len = 2;
      cp = b & 0x1F;
    } else if ((b & 0xF0) == 0xE0) {
      len = 3;
      cp = b & 0x0F;
    } else if ((b & 0xF8) == 0xF0) {
      len = 4;
      cp = b & 0x07;
    }
    bool ok = len > 0 && i + (size_t)len <= n;
    for (int k = 1; ok && k < len; k++) {
      const uint8_t c = (uint8_t)utf8[i + (size_t)k];
      if ((c & 0xC0) != 0x80) {
        ok = false;
      } else {
        cp = (cp << 6) | (c & 0x3F);
      }
    }
    // Overlong forms and values past Unicode are malformed too.
    static const uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (ok && (cp < kMin[len] || cp > 0x10FFFF)) ok = false;
    if (!ok) {
      out += '?';
      i++;  // Resynchronise on the next byte
      continue;
    }
    out += text_fold_detail::fold(cp);
    i += (size_t)len;
  }
  return out;
}

}  // namespace skgauges
