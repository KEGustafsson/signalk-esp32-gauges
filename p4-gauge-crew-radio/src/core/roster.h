// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Who is on the channel, from hellos and audio, as the plugin's node.js and
// the app's roster: every accepted packet refreshes its sender, a hello sets
// the name, transports, hops (hello ttl - header ttl) and build, audio
// within kTalkMs means talking, and a node silent for kSilenceMs is dropped.
// At most kMax nodes; a newcomer beyond that is not added. Owned by the
// network task; the UI gets copies (snapshot()).

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "hello.h"
#include "link_quality.h"

namespace crewradio {

struct Member {
  int32_t id = 0;
  std::string name;  // Empty until the first hello
  uint8_t transports = 0;
  int hops = 0;
  uint16_t version_code = 0;
  bool talking = false;
  int bars = LinkQuality::kBars;  // 1-4, link_quality.h
};

class Roster {
 public:
  static constexpr int kMax = 64;
  static constexpr int64_t kSilenceMs = 4000;
  static constexpr int64_t kTalkMs = 400;

  // Any accepted packet; false when the table is full and `id` is new.
  bool seen(int32_t id, int64_t now_ms);
  // A hello, with the hellos missing before it (Ingress::hello_gap).
  void hello(int32_t id, const Hello& hello, int hops, int64_t now_ms,
             int gap = 0);
  // An audio frame, with the frames missing before it (admit_audio).
  void audio(int32_t id, int64_t now_ms, int gap = 0);

  // Drops nodes silent for kSilenceMs; true if any went.
  bool expire(int64_t now_ms);

  // Everyone, sorted by name (then id), so nobody moves when someone joins.
  void snapshot(int64_t now_ms, std::vector<Member>* out) const;
  int size() const;
  bool anyone_talking(int64_t now_ms) const;

 private:
  struct Node {
    Member m;
    int64_t last_seen_ms = 0;
    int64_t last_audio_ms = INT64_MIN / 2;
    int64_t last_hello_ms = 0;  // Before any hello: the first packet
    LinkQuality link;
    bool used = false;
  };
  Node* find(int32_t id);

  Node nodes_[kMax];
};

}  // namespace crewradio
