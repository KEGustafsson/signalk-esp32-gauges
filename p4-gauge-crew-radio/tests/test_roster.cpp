// SPDX-License-Identifier: EUPL-1.2
// The roster, as the plugin's node.js.
#include "core/roster.h"
#include "test.h"

using namespace crewradio;

namespace {

Hello hello(const char* name, uint8_t transports = Hello::kLan) {
  Hello h;
  h.name = name;
  h.transports = transports;
  h.ttl = 4;
  return h;
}

}  // namespace

TEST(roster_hellos_name_members_sorted_by_name) {
  Roster r;
  r.hello(2, hello("Pekka"), 0, 0);
  r.hello(1, hello("Anna", Hello::kLan | Hello::kReticulum), 1, 0);
  std::vector<Member> m;
  r.snapshot(0, &m);
  CHECK(m.size() == 2 && m[0].name == "Anna" && m[1].name == "Pekka");
  CHECK(m[0].hops == 1 && m[0].transports == 9);
}

TEST(roster_audio_before_a_hello_makes_a_nameless_talker) {
  Roster r;
  r.audio(5, 100);
  std::vector<Member> m;
  r.snapshot(200, &m);
  CHECK(m.size() == 1 && m[0].name.empty() && m[0].talking);
  r.snapshot(500, &m);
  CHECK(!m[0].talking);  // 400 ms after the last frame
  CHECK(r.anyone_talking(499) && !r.anyone_talking(500));
}

TEST(roster_a_silent_node_is_dropped_after_four_seconds) {
  Roster r;
  r.hello(1, hello("Anna"), 0, 0);
  r.hello(2, hello("Juha"), 0, 3000);
  CHECK(!r.expire(4000));
  CHECK(r.expire(4001));
  CHECK(r.size() == 1);
}

TEST(roster_is_bounded) {
  Roster r;
  for (int i = 0; i < Roster::kMax; i++) CHECK(r.seen(i, 0));
  CHECK(!r.seen(1000, 0));
  CHECK(r.seen(5, 0));
}
