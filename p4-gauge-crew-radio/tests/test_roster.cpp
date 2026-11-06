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

// The bars: the app's LinkQuality, rule for rule.
TEST(link_quality_hellos_set_the_bars) {
  LinkQuality q;
  CHECK(q.level(0, 1 << 30) == 4);  // Nothing heard yet: no verdict against it
  q.hello_heard(5);                 // The gap before the first is history
  for (int i = 0; i < 9; i++) q.hello_heard(0);
  CHECK(q.level(0, 1 << 30) == 4);
  q.hello_heard(1);  // One lost in the last ten
  CHECK(q.level(0, 1 << 30) == 3);
  q.hello_heard(1);
  CHECK(q.level(0, 1 << 30) == 2);
  q.hello_heard(2);
  CHECK(q.level(0, 1 << 30) == 1);
  for (int i = 0; i < 10; i++) q.hello_heard(0);  // The window forgets
  CHECK(q.level(0, 1 << 30) == 4);
  q.hello_heard(-1);  // A late one counts nothing
  CHECK(q.level(0, 1 << 30) == 4);
}

TEST(link_quality_an_overdue_hello_counts_as_missing) {
  LinkQuality q;
  for (int i = 0; i < 10; i++) q.hello_heard(0);
  CHECK(q.level(LinkQuality::kHelloGraceMs, 1 << 30) == 4);
  CHECK(q.level(LinkQuality::kHelloGraceMs + 1, 1 << 30) == 3);  // 1 of 11
  CHECK(q.level(LinkQuality::kHelloGraceMs + 1001, 1 << 30) == 2);
  CHECK(q.level(LinkQuality::kHelloGraceMs + 2001, 1 << 30) == 1);
}

TEST(link_quality_audio_loss_needs_a_second_of_talk_and_is_forgotten) {
  LinkQuality q;
  for (int i = 0; i < 10; i++) q.hello_heard(0);
  q.audio_heard(0);
  q.audio_lost(10, 20);  // 10 of 11: but too few frames to judge
  q.audio_heard(20);
  CHECK(q.level(0, 0) == 4);
  for (int i = 0; i < 48; i++) q.audio_heard(40 + i * 20);  // 50 heard, 10 lost
  CHECK(q.level(0, 0) == 2);  // 10 / 60 = 17 %: two bars
  CHECK(q.level(0, LinkQuality::kAudioMemoryMs + 1) == 4);  // Set aside
  // Next transmission after a long silence starts clean.
  q.audio_lost(3, 100000);
  for (int i = 0; i < 60; i++) q.audio_heard(100000 + i * 20);
  CHECK(q.level(0, 0) == 4);
}

TEST(roster_members_carry_their_bars) {
  Roster r;
  Hello h = hello("Anna");
  r.hello(1, h, 0, 0, 0);
  for (int i = 1; i < 10; i++) r.hello(1, h, 0, i * 1000, 0);
  r.hello(1, h, 0, 10000, 2);  // Two hellos lost
  std::vector<Member> m;
  r.snapshot(10000, &m);
  CHECK(m.size() == 1 && m[0].bars == 2);
  r.snapshot(10000 + LinkQuality::kHelloGraceMs + 1, &m);
  CHECK(m[0].bars == 1);  // And one more overdue
}
