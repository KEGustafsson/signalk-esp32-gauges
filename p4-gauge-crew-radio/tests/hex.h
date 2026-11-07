// SPDX-License-Identifier: EUPL-1.2
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

inline std::vector<uint8_t> unhex(const std::string& s) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < s.size(); i += 2) {
    out.push_back((uint8_t)std::stoul(s.substr(i, 2), nullptr, 16));
  }
  return out;
}

inline std::string hex(const uint8_t* p, size_t len) {
  std::string s;
  char b[3];
  for (size_t i = 0; i < len; i++) {
    snprintf(b, sizeof(b), "%02x", p[i]);
    s += b;
  }
  return s;
}

inline std::string read_file(const char* path) {
  std::string s;
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}
