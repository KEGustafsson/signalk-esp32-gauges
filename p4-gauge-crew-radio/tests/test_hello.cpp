// SPDX-License-Identifier: EUPL-1.2
// The roster heartbeat and the name rule, as the app's HelloTest.
#include <string.h>

#include "core/hello.h"
#include "test.h"

using namespace crewradio;

namespace {

bool decode(const std::vector<uint8_t>& p, Hello* out) {
  return Hello::decode(p.data(), p.size(), out);
}

std::vector<uint8_t> raw(const char* name, size_t name_len) {
  std::vector<uint8_t> p = {2, 1, 4, 0, 0, (uint8_t)name_len};
  p.insert(p.end(), name, name + name_len);
  return p;
}

}  // namespace

TEST(hello_round_trips_and_build_is_16_bits_big_endian) {
  Hello h;
  h.name = "Nav station";
  h.transports = Hello::kLan | Hello::kReticulum;
  h.ttl = 4;
  h.version_code = 0x1234;
  uint8_t buf[Hello::kMaxSize];
  const size_t n = h.encode(buf);
  CHECK(n == Hello::kHead + 11);
  CHECK(buf[3] == 0x12 && buf[4] == 0x34);
  Hello back;
  CHECK(Hello::decode(buf, n, &back));
  CHECK(back.name == "Nav station" && back.transports == 9 && back.ttl == 4);
  CHECK(back.version_code == 0x1234);
}

TEST(hello_truncates_long_names_on_a_code_point_boundary) {
  Hello h;
  // 31 ASCII bytes and a two-byte letter: the letter would end at byte 33.
  h.name = std::string(31, 'a') + "\xC3\xA4";
  uint8_t buf[Hello::kMaxSize];
  const size_t n = h.encode(buf);
  CHECK(buf[5] == 31 && n == Hello::kHead + 31);
  Hello back;
  CHECK(Hello::decode(buf, n, &back));
  h.name = std::string(30, 'a') + "\xC3\xA4";  // Fits exactly
  CHECK(h.encode(buf) == Hello::kHead + 32);
}

TEST(hello_an_empty_name_is_allowed) {
  Hello h;
  uint8_t buf[Hello::kMaxSize];
  const size_t n = h.encode(buf);
  Hello back;
  CHECK(n == Hello::kHead && Hello::decode(buf, n, &back) && back.name.empty());
}

TEST(hello_rejects_payloads_off_the_contract) {
  Hello out;
  auto p = raw("Bo", 2);
  CHECK(decode(p, &out));
  auto bad = p;
  bad[0] = 1;  // Version 1: no build number, not accepted
  CHECK(!decode(bad, &out));
  bad = p;
  bad.push_back('x');  // Trailing byte
  CHECK(!decode(bad, &out));
  bad = p;
  bad.pop_back();  // Short
  CHECK(!decode(bad, &out));
  std::string long_name(33, 'n');
  CHECK(!decode(raw(long_name.c_str(), 33), &out));
  CHECK(!decode(raw("\xC3", 1), &out));          // Truncated sequence
  CHECK(!decode(raw("\xC0\xAF", 2), &out));      // Overlong '/'
  CHECK(!decode(raw("\xED\xA0\x80", 3), &out));  // A surrogate
  CHECK(!decode(raw("\xF4\x90\x80\x80", 4), &out));  // Above U+10FFFF
  CHECK(!Hello::decode(p.data(), 5, &out));
}

TEST(hello_names_lose_control_and_format_characters) {
  CHECK(sanitise_name("Bo\x07" "at\x1B[31m") == "Boat[31m");
  CHECK(sanitise_name("a\tb") == "ab");  // A tab is a control: dropped
  // Bidi override, zero-width space, byte order mark, soft hyphen.
  CHECK(sanitise_name("\xE2\x80\xAE" "evil\xE2\x80\x8B" "x\xEF\xBB\xBF" "y\xC2\xAD" "z") ==
        "evilxyz");
  // A tag character above the basic plane (U+E0041).
  CHECK(sanitise_name("ok\xF3\xA0\x81\x81") == "ok");
  // C1 controls (U+0085).
  CHECK(sanitise_name("a\xC2\x85" "b") == "ab");
}

TEST(hello_names_collapse_white_space_and_trim) {
  CHECK(sanitise_name("  Mikko \xC2\xA0\xE3\x80\x80 S25  ") == "Mikko S25");
  CHECK(sanitise_name("\xE2\x80\xA8") == "");
  CHECK(sanitise_name("P\xC3\xA4ivi") == "P\xC3\xA4ivi");  // Letters stay
}
