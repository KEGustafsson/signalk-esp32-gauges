// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Cryptographically secure random bytes: the one thing the core needs from
// the platform. On the ESP32 it is esp_fill_random() (random_esp.cpp; the
// hardware RNG, which ESP-IDF documents as true random while the radio is
// running); the host tests link their own (tests/random_host.cpp).

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace port {

void random_bytes(uint8_t* out, size_t len);

}  // namespace port
}  // namespace crewradio
