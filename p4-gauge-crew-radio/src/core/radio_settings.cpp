// SPDX-License-Identifier: EUPL-1.2
#include "radio_settings.h"

#include <ctype.h>

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

namespace {

bool v6_char(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F') || c == ':' || c == '.';
}

bool name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '-';
}

// 1-65535, digits only and no leading zero (written as the number prints).
bool parse_port_text(const std::string& t, int* port) {
  if (t.empty() || t.size() > 5 || t[0] == '0') return false;
  int v = 0;
  for (char c : t) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  if (v < 1 || v > 65535) return false;
  *port = v;
  return true;
}

}  // namespace

bool parse_host_port(const std::string& text, std::string* host, int* port) {
  size_t a = 0, b = text.size();
  while (a < b && isspace((unsigned char)text[a])) a++;
  while (b > a && isspace((unsigned char)text[b - 1])) b--;
  const std::string t = text.substr(a, b - a);
  if (t.empty() || t.size() > 260) return false;
  std::string h;
  std::string port_text;
  bool has_port = false;
  size_t colons = 0;
  for (char c : t) colons += c == ':' ? 1 : 0;
  if (t[0] == '[') {
    const size_t end = t.find(']');
    if (end == std::string::npos || end < 2) return false;
    h = t.substr(1, end - 1);
    const std::string rest = t.substr(end + 1);
    if (!rest.empty()) {
      if (rest[0] != ':') return false;
      port_text = rest.substr(1);
      has_port = true;
    }
    if (h.find(':') == std::string::npos) return false;
    for (char c : h) {
      if (!v6_char(c)) return false;
    }
  } else if (colons > 1) {  // A bare IPv6 address, no port
    h = t;
    for (char c : h) {
      if (!v6_char(c)) return false;
    }
  } else {
    const size_t colon = t.find(':');
    h = colon == std::string::npos ? t : t.substr(0, colon);
    if (colon != std::string::npos) {
      port_text = t.substr(colon + 1);
      has_port = true;
    }
    if (h.empty() || h.size() > 253 || h[0] == '-' || h[0] == '.') return false;
    for (char c : h) {
      if (!name_char(c)) return false;
    }
  }
  int p = kDefaultReticulumPort;
  if (has_port && !parse_port_text(port_text, &p)) return false;
  *host = h;
  *port = p;
  return true;
}

}  // namespace settings
}  // namespace crewradio
