// SPDX-License-Identifier: EUPL-1.2
// The power-on self-test, and that its compiled-in bytes are the vector's.
#include <ArduinoJson.h>

#include <vector>

#include "core/self_test.h"
#include "hex.h"
#include "test.h"

using namespace crewradio;

TEST(self_test_passes) { CHECK(self_test::run()); }

TEST(self_test_bytes_are_the_vector_files) {
  JsonDocument doc;
  CHECK(deserializeJson(doc, read_file(VECTOR_DIR "/vector.json")) ==
        DeserializationError::Ok);
  CHECK(hex(self_test::kKey, 32) == doc["derivedKeyHex"].as<std::string>());
  CHECK(hex(self_test::kPacket, self_test::kPacketLen) ==
        doc["packetHex"].as<std::string>());
  CHECK(hex(self_test::kPlain, self_test::kPlainLen) ==
        doc["plainHex"].as<std::string>());
}
