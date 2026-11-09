// SPDX-License-Identifier: EUPL-1.2
#pragma once

// One forwarder per boat, as the app's and the plugin's (crew-radio's
// "radio under load" rules, 2026-11-08). Every node on the boat's WLAN that
// is also on Reticulum used to bridge everything it heard to every
// Reticulum link, and the boat's nodes were linked to each other through
// the hub as well: one talker put about ten copies of each 20 ms frame
// through the hub, two talkers stalled it, and every client dropped off.
//
// Now, from what each node hears and nothing else (no messages):
//
//  - A sender is *local* while an unrelayed hello of its (hops == ttl) has
//    come over the WLAN within kLocalMs.
//  - The boat's forwarder: of this node (when its own hellos say kRnsUp) and
//    every local sender whose last hello said kRnsUp, the highest rank
//    (kFwdPrefer 2, neither 1, kFwdAvoid 0), then the lowest sender id as an
//    unsigned 32-bit number. This node says kFwdAvoid: little RAM.
//  - Audio first heard on the WLAN goes to Reticulum only from the
//    forwarder, and only when its talker is not on Reticulum itself (a
//    talker that is sends its own voice); audio first heard over Reticulum
//    goes to the WLAN only from the forwarder. Hellos keep their relay.
//  - Split horizon: nothing goes over Reticulum to a link whose far end is a
//    local sender. A link's far end is learnt from the first authentic
//    packet that came on it straight from its sender (hops == ttl).
//
// Views can differ for a moment; the rules are such that a difference makes
// a duplicate (dropped by the seen-cache), never a gap. Pure; owned by the
// network task.

#include <stddef.h>
#include <stdint.h>

#include "hello.h"

namespace crewradio {

class Forwarder {
 public:
  static constexpr int64_t kLocalMs = 15000;
  static constexpr int kMaxLocal = 32;
  // Link tags: the Reticulum node's slot (top 8 bits) and that slot's epoch
  // (24 bits), so a slot reused by another link is not taken for the old.
  static constexpr int kMaxLinks = 32;

  // The hello's transports bits it reads (hello.h).
  static constexpr uint8_t kRnsUp = Hello::kRnsUp;
  static constexpr uint8_t kFwdPrefer = Hello::kFwdPrefer;
  static constexpr uint8_t kFwdAvoid = Hello::kFwdAvoid;

  static constexpr uint32_t make_tag(int slot, uint32_t epoch) {
    return ((uint32_t)slot << 24) | (epoch & 0xFFFFFFu);
  }
  static constexpr int tag_slot(uint32_t tag) { return (int)(tag >> 24); }

  // An unrelayed, authentic, fresh hello over the WLAN.
  void local_hello(int32_t sender, uint8_t transports, int64_t now_ms);
  bool is_local(int32_t sender, int64_t now_ms) const;
  // Local, and its last hello said kRnsUp.
  bool local_rns_up(int32_t sender, int64_t now_ms) const;

  // The forwarder's sender id, given this node's own id and hello bits;
  // false when there is none (nobody on Reticulum).
  bool forwarder(int32_t self, uint8_t own, int64_t now_ms,
                 int32_t* id) const;
  // True when this node is the boat's forwarder now.
  bool is_forwarder(int32_t self, uint8_t own, int64_t now_ms) const {
    int32_t id = 0;
    return forwarder(self, own, now_ms, &id) && id == self;
  }

  // Where audio first heard on `from_lan` (else over Reticulum) goes.
  struct Route {
    bool to_lan = false;
    bool to_reticulum = false;
  };
  static Route audio_route(bool from_lan, bool am_forwarder,
                           bool talker_rns_up);

  // An authentic packet straight from `sender` came on the link `tag`.
  void link_heard(uint32_t tag, int32_t sender);
  // The tag of slot's link when its far end is local (no copies to it),
  // else 0 (no slot uses tag 0: epochs start at 1).
  uint32_t skip_tag(int slot, int64_t now_ms) const;

  static int rank(uint8_t transports);

 private:
  struct Local {
    bool used = false;
    int32_t sender = 0;
    uint8_t transports = 0;
    int64_t at = 0;
  };
  struct LinkEnd {
    uint32_t tag = 0;  // 0: unknown
    int32_t sender = 0;
  };
  const Local* find(int32_t sender, int64_t now_ms) const;

  Local local_[kMaxLocal];
  LinkEnd links_[kMaxLinks];
};

}  // namespace crewradio
