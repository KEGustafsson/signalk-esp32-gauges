// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Cryptographically secure random bytes: the one thing the core needs from
// the platform. On the ESP32-P4 (random_esp.cpp) an HMAC-DRBG seeded from
// the hardware RNG with its SAR ADC entropy source switched on, since the P4
// has no radio of its own to make esp_fill_random() true random, and fed
// fresh hardware bytes on every draw. Thread-safe. The host tests link their
// own (tests/random_host.cpp).

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace port {

void random_bytes(uint8_t* out, size_t len);

}  // namespace port
}  // namespace crewradio
