// SPDX-License-Identifier: EUPL-1.2
// The Reticulum bytes the app's RnsVectorTest and the plugin's rns.test.js
// check (vectors/rns.vector.json, copied from crew-radio's
// sk-plugin/test/): checked against RNS 1.5.4 itself when the vector was
// made. Identity and destination hashes, the announce and its signature, the
// link request, proof, key and token, the RTT, the Carry cut, the key proofs.
#include <ArduinoJson.h>
#include <string.h>

#include "core/rns/carry.h"
#include "core/rns/rns_identity.h"
#include "core/rns/rns_link.h"
#include "core/rns/rns_packet.h"
#include "hex.h"
#include "test.h"

using namespace crewradio::rns;

namespace {

JsonDocument load() {
  JsonDocument doc;
  const std::string text =
      read_file((std::string(VECTOR_DIR) + "/rns.vector.json").c_str());
  CHECK(!text.empty());
  CHECK(deserializeJson(doc, text) == DeserializationError::Ok);
  return doc;
}

std::vector<uint8_t> b(JsonVariantConst v) { return unhex(v.as<std::string>()); }

Identity identity_of(const JsonDocument& doc) {
  return Identity(b(doc["identity"]["encPriv"]).data(),
                  b(doc["identity"]["sigSeed"]).data());
}

// The vector's announce, as built here.
std::vector<uint8_t> the_announce(const JsonDocument& doc, const Identity& id) {
  const auto nh = b(doc["nameHash"]);
  const auto random = b(doc["announce"]["random"]);
  uint8_t dest[kHashBytes], data[kAnnounceMin], raw[kMtu];
  const size_t n = build_announce(id, nh.data(), nullptr, 0,
                                  doc["announce"]["time"].as<int64_t>(), dest,
                                  data, random.data());
  const size_t len = encode(raw, kAnnounce, kSingle, dest, kCtxNone, data, n);
  return std::vector<uint8_t>(raw, raw + len);
}

}  // namespace

TEST(rns_vector_tag_name_identity_and_destination) {
  const JsonDocument doc = load();
  const Identity id = identity_of(doc);
  uint8_t nh[kNameHashBytes];
  name_hash(doc["destinationName"].as<std::string>().c_str(), nh);
  CHECK(hex(nh, sizeof(nh)) == doc["nameHash"].as<std::string>());
  CHECK(doc["destinationName"].as<std::string>() ==
        "crewradio.channel." + doc["reticulumTag"].as<std::string>());
  CHECK(hex(id.public_key(), 64) == doc["identity"]["publicKey"].as<std::string>());
  CHECK(hex(id.hash(), 16) == doc["identity"]["hash"].as<std::string>());
  uint8_t dest[kHashBytes];
  destination_hash(nh, id.hash(), dest);
  CHECK(hex(dest, 16) == doc["announce"]["destination"].as<std::string>());
}

TEST(rns_vector_the_announce_byte_for_byte_framed_and_parsed) {
  const JsonDocument doc = load();
  const Identity id = identity_of(doc);
  const auto raw = the_announce(doc, id);
  CHECK(hex(raw.data(), raw.size()) == doc["announce"]["raw"].as<std::string>());
  uint8_t framed[kMaxFrame];
  const size_t n = frame(raw.data(), raw.size(), framed);
  CHECK(hex(framed, n) == doc["announce"]["framed"].as<std::string>());
  Packet p;
  CHECK(decode(raw.data(), raw.size(), &p));
  Announce a;
  CHECK(parse_announce(p, &a));
  CHECK(a.emitted == doc["announce"]["time"].as<int64_t>());
}

TEST(rns_vector_link_request_proof_key_token_and_rtt) {
  const JsonDocument doc = load();
  const Identity id = identity_of(doc);
  const auto raw = the_announce(doc, id);
  Packet p;
  CHECK(decode(raw.data(), raw.size(), &p));
  Announce a;
  CHECK(parse_announce(p, &a));

  Request req;
  request(a, b(doc["linkRequest"]["transportId"]).data(), &req,
          b(doc["linkRequest"]["encPriv"]).data(),
          b(doc["linkRequest"]["sigSeed"]).data());
  CHECK(hex(req.raw, req.raw_len) == doc["linkRequest"]["raw"].as<std::string>());
  CHECK(hex(req.id, 16) == doc["linkRequest"]["linkId"].as<std::string>());

  Packet rp;
  CHECK(decode(req.raw, req.raw_len, &rp));
  Link responder;
  uint8_t proof[kMtu];
  size_t proof_len = 0;
  CHECK(accept(id, rp, &responder, proof, &proof_len,
               b(doc["linkProof"]["encPriv"]).data()));
  CHECK(hex(proof, proof_len) == doc["linkProof"]["raw"].as<std::string>());
  CHECK(hex(responder.key_for_test(), 64) == doc["linkProof"]["key"].as<std::string>());

  Packet pp;
  CHECK(decode(proof, proof_len, &pp));
  Link link;
  CHECK(req.complete(pp, &link));
  CHECK(hex(link.key_for_test(), 64) == doc["linkProof"]["key"].as<std::string>());

  const auto plain = b(doc["token"]["plain"]);
  const auto iv = b(doc["token"]["iv"]);
  uint8_t token[kMtu];
  const size_t tn = token_encrypt(link.key_for_test(), plain.data(), plain.size(),
                                  token, iv.data());
  CHECK(hex(token, tn) == doc["token"]["token"].as<std::string>());
  uint8_t data_raw[kMtu];
  const size_t dn = link.data_packet(plain.data(), plain.size(), data_raw, iv.data());
  CHECK(hex(data_raw, dn) == doc["token"]["dataRaw"].as<std::string>());

  uint8_t rtt[9];
  pack_rtt(doc["rtt"]["seconds"].as<double>(), rtt);
  CHECK(hex(rtt, 9) == doc["rtt"]["packed"].as<std::string>());
}

TEST(rns_vector_the_key_proof_of_each_end_of_a_link) {
  const JsonDocument doc = load();
  const auto ck = b(doc["keyProof"]["confirmKey"]);
  const auto id = b(doc["keyProof"]["linkId"]);
  uint8_t proof[carry::kProofBytes];
  carry::key_proof(ck.data(), id.data(), true, proof);
  CHECK(hex(proof, sizeof(proof)) == doc["keyProof"]["initiator"].as<std::string>());
  carry::key_proof(ck.data(), id.data(), false, proof);
  CHECK(hex(proof, sizeof(proof)) == doc["keyProof"]["responder"].as<std::string>());
  const auto initiator = b(doc["keyProof"]["initiator"]);
  CHECK(carry::proof_matches(initiator.data(), initiator.size(), ck.data(), id.data(), true));
  CHECK(!carry::proof_matches(initiator.data(), initiator.size(), ck.data(), id.data(), false));
  const uint8_t other[16] = {};
  CHECK(!carry::proof_matches(initiator.data(), initiator.size(), ck.data(), other, true));
}

TEST(rns_vector_a_pcm_sized_packet_is_cut_the_same_way) {
  const JsonDocument doc = load();
  std::vector<uint8_t> packet(686);
  for (size_t i = 0; i < packet.size(); i++) packet[i] = (uint8_t)i;
  uint8_t parts[carry::kMaxParts][kMdu];
  size_t lens[carry::kMaxParts];
  const size_t n = carry::cut(packet.data(), packet.size(),
                              (uint8_t)doc["carry"]["id"].as<int>(), parts, lens);
  JsonArrayConst expected = doc["carry"]["parts"].as<JsonArrayConst>();
  CHECK(n == expected.size());
  for (size_t i = 0; i < n && i < expected.size(); i++) {
    CHECK(hex(parts[i], lens[i]) == expected[i].as<std::string>());
  }
}
