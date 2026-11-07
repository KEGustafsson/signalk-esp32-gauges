// SPDX-License-Identifier: EUPL-1.2
// The host's random bytes for the tests: /dev/urandom, or on native Windows
// (MinGW, which has none) the C++ library's random_device (rand_s there).
#include <stdio.h>
#include <stdlib.h>

#if defined(_WIN32)
#include <random>
#endif

#include "port/random.h"

namespace crewradio {
namespace port {

void random_bytes(uint8_t* out, size_t len) {
#if defined(_WIN32)
  static std::random_device rd;
  for (size_t i = 0; i < len; i++) out[i] = (uint8_t)(rd() & 0xFF);
#else
  FILE* f = fopen("/dev/urandom", "rb");
  if (f == nullptr || fread(out, 1, len, f) != len) abort();
  fclose(f);
#endif
}

}  // namespace port
}  // namespace crewradio
