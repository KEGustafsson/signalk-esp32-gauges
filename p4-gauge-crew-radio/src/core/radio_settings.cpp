// SPDX-License-Identifier: EUPL-1.2
#include "radio_settings.h"

namespace crewradio {
namespace settings {

bool valid_channel_key(const std::string& key) {
  if ((int)key.size() < kKeyMin || (int)key.size() > kKeyMax) return false;
  for (char c : key) {
    if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7E) return false;
  }
  return key.front() != ' ' && key.back() != ' ';
}

bool parse_group(const std::string& text, uint32_t* group) {
  uint32_t value = 0;
  int octets = 0;
  size_t i = 0;
  while (octets < 4) {
    // One octet: "0" or 1-3 digits without a leading zero, at most 255.
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
    if (text[i] == '0' && i + 1 < text.size() && text[i + 1] >= '0' &&
        text[i + 1] <= '9') {
      return false;
    }
    int v = 0;
    int digits = 0;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
      v = v * 10 + (text[i] - '0');
      i++;
      if (++digits > 3) return false;
    }
    if (v > 255) return false;
    value = (value << 8) | (uint32_t)v;
    octets++;
    if (octets < 4) {
      if (i >= text.size() || text[i] != '.') return false;
      i++;
    }
  }
  if (i != text.size()) return false;
  const uint32_t first = value >> 24;
  if (first < 224 || first > 239) return false;
  *group = value;
  return true;
}

bool valid_port(int port) { return port >= 1024 && port <= 65535; }

bool valid_talk_gpio(int gpio) {
  if (gpio == -1) return true;
  if (gpio < 0 || gpio > 54) return false;
  static const int kTaken[] = {7,  8,  9,  10, 11, 12, 13, 24, 25, 26,
                               27, 39, 40, 41, 42, 43, 44, 53};
  for (int t : kTaken) {
    if (gpio == t) return false;
  }
  return true;
}

std::string mask_key(const std::string& key) {
  if (key.size() < 4) return "";
  return "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 "
         "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2 " +
         key.substr(key.size() - 4);
}

}  // namespace settings
}  // namespace crewradio
