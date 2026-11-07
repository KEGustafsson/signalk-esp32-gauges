// SPDX-License-Identifier: EUPL-1.2
#pragma once

// How well one crew member's packets reach this node, as the 1-4 bars on
// the Crew page: the app's LinkQuality.kt, rule for rule. Measured on the
// packets themselves (a gap in a sender's hello or audio sequence is a
// packet that did not arrive), never on a radio level the node cannot see:
//
//  - the last kHelloWindow hellos: none missing 4 bars, up to one in ten 3,
//    two 2, more 1;
//  - the last kAudioWindow audio frames (five seconds of talk): up to 2 %
//    lost 4, up to 8 % 3, up to 20 % 2, worse 1; believed only after
//    kAudioMin frames, set aside after kAudioMemoryMs of silence;
//  - the worse of the two. A hello overdue now counts as missing (one a
//    second past kHelloGraceMs), so a node that went quiet loses a bar a
//    second until the roster drops it.
//
// The gap before an entry's first hello or audio frame is history, not
// loss (the sender numbered those while nobody listened). Pure; owned by
// the network task.

#include <stdint.h>

namespace crewradio {

class LinkQuality {
 public:
  static constexpr int kBars = 4;
  static constexpr int kWeak = 1;
  static constexpr int kHelloWindow = 10;
  static constexpr int kAudioWindow = 250;
  static constexpr int kAudioMin = 50;
  static constexpr int64_t kHelloGraceMs = 1500;
  static constexpr int64_t kAudioMemoryMs = 10000;

  // A hello arrived with `gap` missing before it (a late one: negative).
  void hello_heard(int gap) {
    if (gap < 0) return;
    if (hellos_.total > 0) hellos_.lost(gap);
    hellos_.heard();
  }
  // `n` audio frames were missing before the one admitted at now_ms.
  void audio_lost(int n, int64_t now_ms) {
    freshen(now_ms);
    if (audio_.total > 0) audio_.lost(n);
  }
  void audio_heard(int64_t now_ms) {
    freshen(now_ms);
    audio_.heard();
  }

  // The bars now: since_hello_ms since the last hello (or, before any, the
  // last packet), since_audio_ms since the last audio frame (large: never).
  int level(int64_t since_hello_ms, int64_t since_audio_ms) const {
    int overdue = 0;
    if (since_hello_ms > kHelloGraceMs) {
      const int64_t o = (since_hello_ms - kHelloGraceMs) / 1000 + 1;
      overdue = o > kHelloWindow ? kHelloWindow : (int)o;
    }
    const int hello_total = hellos_.total + overdue;
    const int hello_level =
        hello_total == 0
            ? kBars
            : bars((double)(hellos_.lost_count + overdue) / hello_total,
                   kHelloSteps);
    const int audio_level =
        (audio_.total < kAudioMin || since_audio_ms > kAudioMemoryMs)
            ? kBars
            : bars((double)audio_.lost_count / audio_.total, kAudioSteps);
    return hello_level < audio_level ? hello_level : audio_level;
  }

 private:
  static constexpr double kHelloSteps[3] = {0.0, 0.1, 0.2};
  static constexpr double kAudioSteps[3] = {0.02, 0.08, 0.2};

  static int bars(double loss, const double (&steps)[3]) {
    int level = kBars;
    for (double step : steps) {
      if (loss > step) {
        level--;
      } else {
        break;
      }
    }
    return level < 1 ? 1 : level;
  }

  void freshen(int64_t now_ms) {
    if (has_audio_ && now_ms - last_audio_ms_ > kAudioMemoryMs) audio_.clear();
    last_audio_ms_ = now_ms;
    has_audio_ = true;
  }

  // The last `N` packets of one kind, each heard or lost, counts alongside.
  template <int N>
  struct Window {
    bool ring[N] = {};  // true = lost
    int next = 0;
    int total = 0;
    int lost_count = 0;
    void heard() { push(false); }
    void lost(int n) {
      if (n > N) n = N;
      for (int i = 0; i < n; i++) push(true);
    }
    void clear() { *this = Window(); }
    void push(bool lost) {
      if (total == N && ring[next]) lost_count--;
      ring[next] = lost;
      if (lost) lost_count++;
      next = (next + 1) % N;
      if (total < N) total++;
    }
  };

  Window<kHelloWindow> hellos_;
  Window<kAudioWindow> audio_;
  int64_t last_audio_ms_ = 0;
  bool has_audio_ = false;
};

}  // namespace crewradio
