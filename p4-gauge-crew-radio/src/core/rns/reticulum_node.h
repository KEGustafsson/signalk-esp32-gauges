// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The Reticulum end node, without the socket: announces, peers, links and
// the carrying of channel packets, driven by on_frame(), tick() and send(),
// writing whole Reticulum packets through a Writer. The app's
// ReticulumNode.kt rule for rule (the plugin's lib/rns/transport.js is the
// same design), plus the plugin's two rules for a connection that died
// without a word, since lwIP, like Node, has no TCP_USER_TIMEOUT:
//
//  - Our destination is crewradio.channel.<tag>, the tag from the packet
//    key (ChannelCrypto::reticulum_tag); the identity is made fresh for each
//    boot and never stored.
//  - Announces stay within the transport nodes' budget (announce_budget.h):
//    on connect (a new identity always has budget), and again every
//    kIdleAnnounceMs while there is no link at all and the budget has room
//    to spare; never in answer to somebody else's, never as a refresh while
//    linked. Of two nodes the one whose destination hash sorts lower dials;
//    the other, on hearing a node it did not know (or any node while it has
//    no link at all), cannot count on being heard in turn: when nothing has
//    dialled it kFallbackMs later, it dials the newcomer itself and redials
//    it from then on as its dialler would.
//  - A link carries nothing but the two ends' key proofs (carry.h) until
//    the far end's has checked out, so a stranger who copies our public name
//    hash and links in learns nothing but that we exist.
//  - Floods: an announce under our name and a link request to us each cost
//    a signature, so they come out of a small budget first; copies of an
//    announce already checked, and requests that could get no slot, cost
//    nothing; a link younger than kGraceMs is never evicted, nor one that has
//    proved the key, nor the peer behind it.
//  - A dead connection: every confirmed link silent kStaleMs with no frame
//    at all from the transport node meanwhile, or, on a connection that has
//    echoed one of our announces before (rnsd sends each it passes on back
//    within a second), an announce the budget says passes followed by
//    kStaleMs without a frame. tick() says so and the owner reconnects.
//
// Nothing is relayed within Reticulum: every node links to every other and
// Reticulum's transport nodes do the multi-hop part, so a channel packet
// received here goes to the WLAN only, and send() puts one on every
// confirmed link. This node neither asks the boat (the app's AskCarry) nor
// answers: those frames are dropped. One owner (the Reticulum task), so no
// locks; every time is the caller's monotonic now_ms. Its tables are fixed
// (about 70 KB, in PSRAM on the node).

#include <stddef.h>
#include <stdint.h>

#include "core/rns/announce_budget.h"
#include "core/rns/carry.h"
#include "core/rns/rns_identity.h"
#include "core/rns/rns_link.h"

namespace crewradio {
namespace rns {

class ReticulumNode {
 public:
  class Writer {
   public:
    virtual ~Writer() = default;
    // One whole Reticulum packet to the transport node.
    virtual void write(const uint8_t* raw, size_t len) = 0;
  };
  class Sink {
   public:
    virtual ~Sink() = default;
    // One sealed channel packet that came on a confirmed link.
    virtual void packet(const uint8_t* p, size_t len) = 0;
  };

  static constexpr int64_t kIdleAnnounceMs = 5 * 60000;
  static constexpr int64_t kFallbackMs = 5000;
  static constexpr int64_t kRedialMs = 11 * 60000;
  static constexpr int kRateReserve = 2;
  static constexpr int64_t kLinkTimeoutMs = 10000;
  static constexpr int64_t kStaleMs = 12000;
  static constexpr int64_t kConfirmMs = 15000;
  static constexpr int64_t kProofResendMs = 2000;
  static constexpr int64_t kGraceMs = 5000;
  static constexpr int kGateBurst = 20;
  static constexpr int kGatePerSecond = 10;
  static constexpr int kVerifiedMax = 256;
  static constexpr int64_t kPeerForgetMs = 30 * 60000;
  static constexpr int kMaxLinks = 32;
  static constexpr int kMaxPeers = 64;
  // A confirmed link's own budget, before anything reaches the channel.
  static constexpr int kLinkRate = 400;
  static constexpr int kLinkBurst = 800;

  struct Counters {
    uint32_t announces = 0;  // Checked, from crew under our name
    uint32_t older = 0;      // Older than one already heard: ignored
    uint32_t echoes = 0;     // Our own, sent back by the transport node
    uint32_t links_up = 0;   // Proved the key
    uint32_t links_dropped = 0;
    uint32_t unproven = 0;    // Closed without proving the key
    uint32_t unanswered = 0;  // Our requests nobody answered
    uint32_t takeovers = 0;   // Peers we dial although they sort lower
    uint32_t tx = 0;          // Channel packets sent
    uint32_t rx = 0;          // Channel packets received
  };

  // `tag` is the 16-hex-digit channel tag. `wall_s`, the wall clock in
  // seconds an announce is stamped with (time() when null).
  ReticulumNode(const char* tag, const uint8_t confirm_key[32], Writer* writer,
                const Identity& identity = Identity::generate(),
                int64_t (*wall_s)() = nullptr);
  ReticulumNode(const ReticulumNode&) = delete;
  ReticulumNode& operator=(const ReticulumNode&) = delete;

  // The transport node is reachable: announce at once.
  void connected(int64_t now);
  // The connection is gone, and every link with it; the peers stay, their
  // paths run through the same transport node.
  void disconnected(int64_t now);
  // Closes every link politely before the connection goes.
  void close_all();
  // A sealed channel packet on every confirmed link. True when it went
  // anywhere.
  bool send(const uint8_t* packet, size_t len);
  // One Reticulum packet from the transport node.
  void on_frame(const uint8_t* raw, size_t len, int64_t now, Sink* sink);
  // Once a second: announces due, requests and links timed out, redials,
  // forgotten peers. False when the connection is dead (above): the owner
  // closes it and calls disconnected().
  bool tick(int64_t now);

  // Links that carry channel traffic: up, and their far end proved the key.
  int link_count() const;
  int peer_count() const;
  int pending_count() const;
  int entry_count() const;  // Links held, confirmed or not
  bool is_connected() const { return connected_; }
  const Counters& counters() const { return c_; }
  const Identity& identity() const { return identity_; }
  const uint8_t* destination() const { return destination_; }
  const uint8_t* name_hash() const { return name_hash_; }

  // For tests.
  bool knows(const uint8_t dest[kHashBytes]) const;
  int64_t peer_seen_at(const uint8_t dest[kHashBytes]) const;  // -1: unknown
  int64_t peer_emitted(const uint8_t dest[kHashBytes]) const;
  bool took_over(const uint8_t dest[kHashBytes]) const;
  int64_t announced_at() const { return last_announce_; }
  int announce_violations() const { return budget_.violations(); }
  double announce_budget_left(int64_t now);
  double request_budget_left(int64_t now);
  bool holds_link(const uint8_t id[kHashBytes]) const;
  // The links held: the i-th used one's link, its confirmed flag.
  const Link* entry_link(int i) const;
  bool entry_confirmed(int i) const;
  void set_entry_confirmed(int i, bool confirmed);
  void set_entry_last_in(int i, int64_t t);

 private:
  struct Entry {
    bool used = false;
    Link link;
    bool has_peer = false;
    uint8_t peer[kHashBytes] = {};
    int64_t created_at = 0;
    int64_t last_in = 0;
    bool confirmed = false;
    int64_t proof_at = 0;
    carry::Joiner joiner;
    uint8_t cut_id = 0;
    int64_t tokens_milli = 0;
    int64_t refilled = 0;
    bool allow(int64_t now);
  };
  struct Peer {
    bool used = false;
    Announce announce;
    bool has_transport = false;
    uint8_t transport_id[kHashBytes] = {};
    int hops = 0;
    int64_t seen_at = 0;
    int64_t emitted = 0;
    int64_t dial_at = 0;
    int64_t backoff_ms = 1000;
    int link = -1;          // Entry index
    bool takeover = false;  // We dial it although its hash sorts lower
    int64_t fallback_at = 0;
    uint32_t accepted_mark = 0;
  };
  struct Pending {
    bool used = false;
    Request request;
    int64_t sent_at = 0;
  };
  // Work a stranger can make us do: kGatePerSecond, bursts of kGateBurst.
  struct Gate {
    bool started = false;
    int64_t milli = 0;
    int64_t at = 0;
    int64_t left(int64_t now);
    bool allow(int64_t now);
  };

  void on_announce(const Packet& p, int64_t now);
  void heard(const Packet& p, const Announce& a, int64_t now);
  void on_link_request(const Packet& p, int64_t now);
  void on_proof(const Packet& p, int64_t now);
  void on_data(const Packet& p, int64_t now, Sink* sink);
  void on_key_proof(Entry& e, const uint8_t* payload, size_t len,
                    int64_t now);
  void send_proof(Entry& e, int64_t now);
  bool we_dial(const uint8_t dest[kHashBytes]) const;
  bool pending_for(const uint8_t dest[kHashBytes]) const;
  bool can_make_room(int64_t now) const;
  bool evict_link(int64_t now);
  bool evict_peer(int64_t now);
  void link_to(Peer& peer, int64_t now);
  int add_link(const Link& link, const uint8_t* peer, int64_t now);
  void close(Entry& e, int64_t now);
  void forget(Entry& e, int64_t now);
  void announce(int64_t now);
  void backoff(Peer& peer, int64_t now);
  int used_slots() const;
  Peer* find_peer(const uint8_t dest[kHashBytes]);
  const Peer* find_peer(const uint8_t dest[kHashBytes]) const;
  Entry* find_entry(const uint8_t id[kHashBytes]);
  Pending* find_pending(const uint8_t id[kHashBytes]);
  bool verified(const uint8_t hash[kHashBytes]) const;
  void mark_verified(const uint8_t hash[kHashBytes]);
  void write(const uint8_t* raw, size_t len) { writer_->write(raw, len); }

  uint8_t confirm_key_[32];
  Writer* writer_;
  Identity identity_;
  int64_t (*wall_s_)();
  uint8_t name_hash_[kNameHashBytes];
  uint8_t destination_[kHashBytes];

  Entry entries_[kMaxLinks];
  Peer peers_[kMaxPeers];
  Pending pending_[kMaxLinks];
  uint8_t verified_[kVerifiedMax][kHashBytes];
  int verified_next_ = 0;
  int verified_count_ = 0;
  Gate announce_gate_;
  Gate request_gate_;

  bool connected_ = false;
  int64_t last_announce_ = 0;
  AnnounceBudget budget_;
  // Links others dialled that proved the key: a newcomer that did dial us
  // shows here.
  uint32_t accepted_confirmed_ = 0;
  // The plugin's dead-connection rules.
  int64_t last_rx_ = 0;
  bool echoes_ = false;
  bool expect_echo_ = false;
  Counters c_;

  // Scratch, so the hot path never allocates.
  uint8_t out_[kMtu];
  uint8_t plain_[kMdu + 16];
  uint8_t reply_[kMtu];
  uint8_t parts_[carry::kMaxParts][kMdu];
};

}  // namespace rns
}  // namespace crewradio
