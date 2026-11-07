// SPDX-License-Identifier: EUPL-1.2
// The crew-radio app's cross-language vectors (vectors/vector.json, the file
// the app's CrossLanguageVectorTest and the plugin's tests read): the same
// key derivation, hello and sealed packet, byte for byte, so this node and a
// phone understand each other.
#include <ArduinoJson.h>
#include <string.h>

#include "core/channel_crypto.h"
#include "core/hello.h"
#include "core/packet.h"
#include "hex.h"
#include "test.h"

using namespace crewradio;

namespace {

JsonDocument load(const char* name) {
  JsonDocument doc;
  const std::string text = read_file((std::string(VECTOR_DIR) + "/" + name).c_str());
  CHECK(!text.empty());
  CHECK(deserializeJson(doc, text) == DeserializationError::Ok);
  return doc;
}

// Derives the key, seals the vector's plaintext under its header and nonce,
// and checks every byte; then parses and opens the expected packet.
void check_packet(JsonVariantConst v) {
  const std::string channel_key = v["channelKey"].as<std::string>();
  uint8_t key[32];
  CHECK(ChannelCrypto::derive((const uint8_t*)channel_key.data(),
                              channel_key.size(), key));
  CHECK(hex(key, 32) == v["derivedKeyHex"].as<std::string>());

  Header h;
  h.codec = (Codec)v["header"]["codec"].as<int>();
  h.ttl = v["header"]["ttl"].as<int>();
  h.hops = v["header"]["hops"].as<int>();
  h.sender_id = (int32_t)v["header"]["senderId"].as<int64_t>();
  h.seq = v["header"]["seq"].as<int32_t>();
  h.time = v["header"]["time"].as<uint32_t>();

  const std::vector<uint8_t> plain = unhex(v["plainHex"].as<std::string>());
  const std::vector<uint8_t> nonce = unhex(v["nonceHex"].as<std::string>());
  const std::vector<uint8_t> expected = unhex(v["packetHex"].as<std::string>());

  std::vector<uint8_t> pkt(packet::kHeader + plain.size() +
                           ChannelCrypto::kOverhead);
  packet::encode_header(h, pkt.data());
  uint8_t aad[packet::kHeader];
  packet::aad_of(pkt.data(), aad);
  ChannelCrypto crypto(key);
  CHECK(crypto.ok());
  CHECK(crypto.seal_with_nonce(aad, sizeof(aad), plain.data(), plain.size(),
                               nonce.data(), pkt.data() + packet::kHeader));
  CHECK(pkt == expected);

  Header parsed;
  CHECK(packet::parse(expected.data(), expected.size(), &parsed));
  CHECK(parsed.sender_id == h.sender_id && parsed.seq == h.seq);
  CHECK(parsed.time == h.time && parsed.codec == h.codec);
  CHECK(parsed.ttl == h.ttl && parsed.hops == h.hops);
  packet::aad_of(expected.data(), aad);
  std::vector<uint8_t> out(expected.size());
  const int n = crypto.open(aad, sizeof(aad), expected.data() + packet::kHeader,
                            expected.size() - packet::kHeader, out.data());
  CHECK(n == (int)plain.size());
  CHECK(memcmp(out.data(), plain.data(), plain.size()) == 0);
}

}  // namespace

TEST(vector_packet_matches_the_app_byte_for_byte) {
  const JsonDocument doc = load("vector.json");
  check_packet(doc.as<JsonVariantConst>());
}

TEST(vector_non_ascii_channel_key_is_its_utf8_bytes) {
  const JsonDocument doc = load("vector.json");
  JsonVariantConst v = doc["nonAscii"];
  // The key in the file is the UTF-8 the app derives from.
  CHECK(hex((const uint8_t*)v["channelKey"].as<const char*>(),
            strlen(v["channelKey"].as<const char*>())) ==
        v["channelKeyUtf8Hex"].as<std::string>());
  check_packet(v);
}

TEST(vector_hello_encodes_and_decodes_as_the_app) {
  const JsonDocument doc = load("vector.json");
  Hello hello;
  hello.name = doc["hello"]["name"].as<std::string>();
  hello.transports = doc["hello"]["transports"].as<uint8_t>();
  hello.ttl = doc["hello"]["ttl"].as<int>();
  hello.version_code = doc["hello"]["versionCode"].as<uint16_t>();
  uint8_t buf[Hello::kMaxSize];
  const size_t n = hello.encode(buf);
  CHECK(hex(buf, n) == doc["plainHex"].as<std::string>());
  Hello back;
  CHECK(Hello::decode(buf, n, &back));
  CHECK(back.name == hello.name && back.transports == hello.transports);
  CHECK(back.ttl == hello.ttl && back.version_code == hello.version_code);
}

TEST(vector_reticulum_tag_and_confirm_key_match_the_plugin) {
  const JsonDocument doc = load("rns.vector.json");
  const std::string channel_key = doc["channelKey"].as<std::string>();
  CHECK(!channel_key.empty());
  uint8_t key[32];
  CHECK(ChannelCrypto::derive((const uint8_t*)channel_key.data(),
                              channel_key.size(), key));
  ChannelCrypto crypto(key);
  char tag[17];
  CHECK(crypto.reticulum_tag(tag));
  CHECK(std::string(tag) == doc["reticulumTag"].as<std::string>());
  uint8_t confirm[32];
  CHECK(crypto.reticulum_confirm_key(confirm));
  bool found = false;
  for (JsonPairConst kv : doc.as<JsonObjectConst>()) {
    JsonVariantConst c = kv.value()["confirmKey"];
    if (!c.isNull()) {
      found = true;
      CHECK(hex(confirm, 32) == c.as<std::string>());
    }
  }
  CHECK(found);
}
