// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The receive decision for one packet, as the app's Ingress.kt, in its
// order:
//
//  1. The global budget, before the packet is opened (the sender id is not
//     authenticated yet, so nothing is charged to a sender).
//  2. Our own frame relayed back: a duplicate, after the global budget, so a
//     flood claiming our id still pays.
//  3. The AEAD check (`open`). A failure charges the junk budget.
//  4. The timestamp: more than kReplayWindowS off our clock is Stale and
//     touches no cache.
//  5. The seen-cache look: a copy is a Duplicate before it costs the sender.
//  6. The sender's budget: a sender over it writes nothing into the cache.
//  7. The seen-cache write.
//
// The app holds a lock around 5-7 because a frame arrives on every
// transport's thread at once. Here one receive task waits on every socket
// (WLAN and Reticulum) and is the only caller, so the steps are atomic by
// construction and the class has no locks. The caches and the sequence
// marks live for the process, not the session: connect/disconnect must
// never clear them, or a replay survives a reconnect.

#include <stddef.h>
#include <stdint.h>

#include "packet.h"
#include "rate_limiter.h"
#include "seen_cache.h"
#include "seq_tracker.h"

namespace crewradio {

class Ingress {
 public:
  // Audio: over five minutes of one talker, or the replay window for several.
  static constexpr size_t kAudioCache = 16384;
  // Hellos: the replay window for a crew of thirty.
  static constexpr size_t kHelloCache = 2048;

  enum class Kind : uint8_t {
    kAccept,     // New and within budget: plain_len bytes of payload
    kDuplicate,  // Authentic, heard already
    kRelayOnly,  // Heard already, but this copy reaches further: forward only
    kStale,      // Authentic, outside the replay window
    kRejected,   // See `why`
  };
  enum class Why : uint8_t {
    kNone,
    kGlobalBudget,  // More than this node will look at
    kUnreadable,    // Not sealed with our key
    kJunkFlood,     // Unreadable, and more than a stray phone would send
    kSenderBudget,  // An authentic sender over its own budget
  };
  struct Result {
    Kind kind;
    Why why;
    int relay_ttl;  // The ttl to forward with; 0 = keep it
    int plain_len;  // kAccept only
  };

  Ingress() : Ingress(RateLimiter::Config(), kAudioCache, kHelloCache) {}
  Ingress(const RateLimiter::Config& limits, size_t audio_cache,
          size_t hello_cache);

  // `open()` returns the payload length or -1, and is called at most once,
  // after the global budget. `self_id` (when has_self) is this node's own
  // sender id.
  template <typename Open>
  Result admit(const Header& h, int64_t now_ms, int64_t now_s, int max_hops,
               Open&& open, bool has_self = false, int32_t self_id = 0) {
    if (!limiter_.allow_global(now_ms)) {
      return {Kind::kRejected, Why::kGlobalBudget, 0, 0};
    }
    if (has_self && h.sender_id == self_id) {
      return {Kind::kDuplicate, Why::kNone, 0, 0};
    }
    const int plain_len = open();
    if (plain_len < 0) {
      return {Kind::kRejected,
              limiter_.allow_junk(now_ms) ? Why::kUnreadable : Why::kJunkFlood,
              0, 0};
    }
    return after_open(h, now_ms, now_s, max_hops, plain_len);
  }

  // Audio frames number themselves consecutively: the frames missing before
  // this one, or -1 for a late one (its slot is concealed already).
  int admit_audio(int32_t sender_id, int32_t seq) {
    return seq_.admit(sender_id, seq);
  }
  // A gap in a sender's hello sequence: missing hellos, for the link meter.
  int hello_gap(int32_t sender_id, int32_t seq) {
    return hello_seq_.admit(sender_id, seq);
  }

  // The ttl a relay forwards with, or 0: capped at the sender's signed
  // budget, decremented from what came in, and only while the packet stays
  // within this node's own max_hops of its origin.
  static int relay_ttl(const Header& h, int max_hops);

 private:
  Result after_open(const Header& h, int64_t now_ms, int64_t now_s,
                    int max_hops, int plain_len);

  RateLimiter limiter_;
  SeenCache seen_;
  SeenCache seen_hellos_;
  SeqTracker seq_;
  SeqTracker hello_seq_;
};

}  // namespace crewradio
