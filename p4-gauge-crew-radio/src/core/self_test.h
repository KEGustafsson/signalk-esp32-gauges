// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The power-on known-answer test: before the radio may go on air it opens,
// parses and re-seals the crew-radio app's vector packet (compiled in, no
// PBKDF2: the vector's derived key is used as is). A failure -- a
// miscompiled cipher, a wrong byte order, a broken mbedTLS -- leaves the
// radio off with "self-test failed"; it never transmits what the crew could
// not read.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {
namespace self_test {

extern const uint8_t kKey[32];
extern const uint8_t kPacket[];
extern const size_t kPacketLen;
extern const uint8_t kPlain[];
extern const size_t kPlainLen;

bool run();

}  // namespace self_test
}  // namespace crewradio
