// SPDX-License-Identifier: EUPL-1.2
#include "roster.h"

#include <algorithm>

namespace crewradio {

Roster::Node* Roster::find(int32_t id) {
  for (Node& n : nodes_) {
    if (n.used && n.m.id == id) return &n;
  }
  return nullptr;
}

bool Roster::seen(int32_t id, int64_t now_ms) {
  Node* n = find(id);
  if (n == nullptr) {
    for (Node& slot : nodes_) {
      if (!slot.used) {
        n = &slot;
        break;
      }
    }
    if (n == nullptr) return false;
    *n = Node();
    n->used = true;
    n->m.id = id;
  }
  n->last_seen_ms = now_ms;
  return true;
}

void Roster::hello(int32_t id, const Hello& hello, int hops, int64_t now_ms,
                   int gap, bool reticulum) {
  if (!seen(id, now_ms)) return;
  Node* n = find(id);
  n->link.hello_heard(gap);
  n->last_hello_ms = now_ms;
  n->has_hello = true;
  n->m.name = hello.name;
  n->m.transports = hello.transports;
  n->m.hops = hops < 0 ? 0 : hops;
  n->m.version_code = hello.version_code;
  n->m.reticulum = reticulum;
}

void Roster::audio(int32_t id, int64_t now_ms, int gap) {
  if (!seen(id, now_ms)) return;
  Node* n = find(id);
  if (gap > 0) n->link.audio_lost(gap, now_ms);
  n->link.audio_heard(now_ms);
  n->last_audio_ms = now_ms;
}

bool Roster::expire(int64_t now_ms) {
  bool changed = false;
  for (Node& n : nodes_) {
    if (n.used && now_ms - n.last_seen_ms > kSilenceMs) {
      n.used = false;
      changed = true;
    }
  }
  return changed;
}

void Roster::snapshot(int64_t now_ms, std::vector<Member>* out) const {
  out->clear();
  for (const Node& n : nodes_) {
    if (!n.used) continue;
    Member m = n.m;
    m.talking = now_ms - n.last_audio_ms < kTalkMs;
    // Before its first hello a member is measured from its last packet, as
    // the app's: an audio-only member talking cleanly keeps its bars.
    const int64_t hello_at = n.has_hello ? n.last_hello_ms : n.last_seen_ms;
    m.bars = n.link.level(now_ms - hello_at, now_ms - n.last_audio_ms);
    out->push_back(m);
  }
  std::sort(out->begin(), out->end(), [](const Member& a, const Member& b) {
    return a.name != b.name ? a.name < b.name : a.id < b.id;
  });
}

int Roster::size() const {
  int n = 0;
  for (const Node& x : nodes_) n += x.used ? 1 : 0;
  return n;
}

bool Roster::anyone_talking(int64_t now_ms) const {
  return talking_count(now_ms) > 0;
}

int Roster::talking_count(int64_t now_ms) const {
  int count = 0;
  for (const Node& n : nodes_) {
    if (n.used && now_ms - n.last_audio_ms < kTalkMs) count++;
  }
  return count;
}

}  // namespace crewradio
