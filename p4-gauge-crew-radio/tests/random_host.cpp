// SPDX-License-Identifier: EUPL-1.2
// The host's random bytes for the tests: /dev/urandom.
#include <stdio.h>
#include <stdlib.h>

#include "port/random.h"

namespace crewradio {
namespace port {

void random_bytes(uint8_t* out, size_t len) {
  FILE* f = fopen("/dev/urandom", "rb");
  if (f == nullptr || fread(out, 1, len, f) != len) abort();
  fclose(f);
}

}  // namespace port
}  // namespace crewradio
