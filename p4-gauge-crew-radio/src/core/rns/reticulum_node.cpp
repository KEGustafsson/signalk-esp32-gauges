// SPDX-License-Identifier: EUPL-1.2
#include "reticulum_node.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

namespace crewradio {
namespace rns {

namespace {
int64_t system_wall_s() { return (int64_t)time(nullptr); }
int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
}  // namespace

// ---- budgets ------------------------------------------------------------

bool ReticulumNode::Entry::allow(int64_t now) {
  tokens_milli += (now - refilled) * kLinkRate;
  if (tokens_milli > (int64_t)kLinkBurst * 1000) {
    tokens_milli = (int64_t)kLinkBurst * 1000;
  }
  refilled = now;
  if (tokens_milli < 1000) return false;
  tokens_milli -= 1000;
  return true;
}

int64_t ReticulumNode::Gate::left(int64_t now) {
  if (!started) {
    started = true;
    milli = (int64_t)kGateBurst * 1000;
    at = now;
  }
  milli += (now - at) * kGatePerSecond;
  if (milli > (int64_t)kGateBurst * 1000) milli = (int64_t)kGateBurst * 1000;
  at = now;
  return milli;
}

bool ReticulumNode::Gate::allow(int64_t now) {
  if (left(now) < 1000) return false;
  milli -= 1000;
  return true;
}

// ---- setup and state ----------------------------------------------------

ReticulumNode::ReticulumNode(const char* tag, const uint8_t confirm_key[32],
                             Writer* writer, const Identity& identity,
                             int64_t (*wall_s)())
    : writer_(writer),
      identity_(identity),
      wall_s_(wall_s != nullptr ? wall_s : system_wall_s) {
  memcpy(confirm_key_, confirm_key, 32);
  char name[64];
  snprintf(name, sizeof(name), "crewradio.channel.%s", tag);
  rns::name_hash(name, name_hash_);
  destination_hash(name_hash_, identity_.hash(), destination_);
}

int ReticulumNode::link_count() const {
  int n = 0;
  for (const Entry& e : entries_) {
    if (e.used && e.link.active() && e.confirmed) n++;
  }
  return n;
}

int ReticulumNode::peer_count() const {
  int n = 0;
  for (const Peer& p : peers_) n += p.used ? 1 : 0;
  return n;
}

int ReticulumNode::pending_count() const {
  int n = 0;
  for (const Pending& p : pending_) n += p.used ? 1 : 0;
  return n;
}

int ReticulumNode::entry_count() const {
  int n = 0;
  for (const Entry& e : entries_) n += e.used ? 1 : 0;
  return n;
}

int ReticulumNode::used_slots() const { return entry_count() + pending_count(); }

ReticulumNode::Peer* ReticulumNode::find_peer(const uint8_t dest[kHashBytes]) {
  for (Peer& p : peers_) {
    if (p.used && memcmp(p.announce.destination, dest, kHashBytes) == 0) {
      return &p;
    }
  }
  return nullptr;
}

const ReticulumNode::Peer* ReticulumNode::find_peer(
    const uint8_t dest[kHashBytes]) const {
  return const_cast<ReticulumNode*>(this)->find_peer(dest);
}

ReticulumNode::Entry* ReticulumNode::find_entry(const uint8_t id[kHashBytes]) {
  for (Entry& e : entries_) {
    if (e.used && memcmp(e.link.id(), id, kHashBytes) == 0) return &e;
  }
  return nullptr;
}

ReticulumNode::Pending* ReticulumNode::find_pending(
    const uint8_t id[kHashBytes]) {
  for (Pending& p : pending_) {
    if (p.used && memcmp(p.request.id, id, kHashBytes) == 0) return &p;
  }
  return nullptr;
}

bool ReticulumNode::pending_for(const uint8_t dest[kHashBytes]) const {
  for (const Pending& p : pending_) {
    if (p.used && memcmp(p.request.peer, dest, kHashBytes) == 0) return true;
  }
  return false;
}

bool ReticulumNode::verified(const uint8_t hash[kHashBytes]) const {
  for (int i = 0; i < verified_count_; i++) {
    if (memcmp(verified_[i], hash, kHashBytes) == 0) return true;
  }
  return false;
}

void ReticulumNode::mark_verified(const uint8_t hash[kHashBytes]) {
  memcpy(verified_[verified_next_], hash, kHashBytes);
  verified_next_ = (verified_next_ + 1) % kVerifiedMax;
  if (verified_count_ < kVerifiedMax) verified_count_++;
}

bool ReticulumNode::we_dial(const uint8_t dest[kHashBytes]) const {
  return memcmp(destination_, dest, kHashBytes) < 0;
}

void ReticulumNode::backoff(Peer& peer, int64_t now) {
  peer.dial_at = now + peer.backoff_ms;
  peer.backoff_ms = peer.backoff_ms * 2 > 15000 ? 15000 : peer.backoff_ms * 2;
}

// ---- the connection -----------------------------------------------------

void ReticulumNode::connected(int64_t now) {
  connected_ = true;
  last_rx_ = now;
  echoes_ = false;
  announce(now);
}

void ReticulumNode::disconnected(int64_t /*now*/) {
  connected_ = false;
  // A peer we just had a link with is as fresh as that link, not as its last
  // announce: the dialler redials it as soon as the connection is back.
  for (Entry& e : entries_) {
    if (!e.used) continue;
    if (e.has_peer) {
      Peer* p = find_peer(e.peer);
      if (p != nullptr) p->seen_at = max64(p->seen_at, e.last_in);
    }
    if (e.link.active() && e.confirmed) c_.links_dropped++;
    e.used = false;
  }
  for (Pending& p : pending_) p.used = false;
  for (Peer& p : peers_) {
    p.link = -1;
    p.dial_at = 0;
  }
}

void ReticulumNode::close_all() {
  for (Entry& e : entries_) {
    if (!e.used) continue;
    write(out_, e.link.close_packet(out_));
  }
}

bool ReticulumNode::send(const uint8_t* packet, size_t len) {
  if (!connected_) return false;
  bool sent = false;
  size_t lens[carry::kMaxParts];
  for (Entry& e : entries_) {
    if (!e.used || !e.link.active() || !e.confirmed) continue;
    const size_t n = carry::cut(packet, len, e.cut_id++, parts_, lens);
    for (size_t i = 0; i < n; i++) {
      const size_t m = e.link.data_packet(parts_[i], lens[i], out_);
      if (m > 0) write(out_, m);
    }
    sent = sent || n > 0;
  }
  if (sent) c_.tx++;
  return sent;
}

// ---- what comes in ------------------------------------------------------

void ReticulumNode::on_frame(const uint8_t* raw, size_t len, int64_t now,
                             Sink* sink) {
  last_rx_ = now;
  Packet p;
  if (!decode(raw, len, &p)) return;
  switch (p.packet_type) {
    case kAnnounce:
      on_announce(p, now);
      break;
    case kLinkRequest:
      on_link_request(p, now);
      break;
    case kProof:
      on_proof(p, now);
      break;
    default:
      on_data(p, now, sink);
      break;
  }
}

void ReticulumNode::on_announce(const Packet& p, int64_t now) {
  if (p.dest_type != kSingle || p.data_len < kAnnounceMin) return;
  // Another channel: no signature check spent.
  if (memcmp(p.data + kPublicBytes, name_hash_, kNameHashBytes) != 0) return;
  if (memcmp(p.destination, destination_, kHashBytes) == 0) {
    echoes_ = true;  // Our own, sent back: the connection works
    c_.echoes++;
    return;
  }
  uint8_t full[32];
  hash_of(full, p.raw, p.raw_len);
  // A copy of one already checked costs no budget; a flood under our public
  // name, no signature check.
  if (verified(full) || !announce_gate_.allow(now)) return;
  Announce a;
  if (!parse_announce(p, &a)) return;
  mark_verified(full);
  c_.announces++;
  heard(p, a, now);
}

void ReticulumNode::heard(const Packet& p, const Announce& a, int64_t now) {
  Peer* peer = find_peer(p.destination);
  if (peer != nullptr && a.emitted < peer->emitted) {
    c_.older++;  // An older announce replayed
    return;
  }
  const bool fresh = peer == nullptr;
  if (fresh) {
    for (Peer& slot : peers_) {
      if (!slot.used) {
        peer = &slot;
        break;
      }
    }
    if (peer == nullptr) {
      if (!evict_peer(now)) return;
      for (Peer& slot : peers_) {
        if (!slot.used) {
          peer = &slot;
          break;
        }
      }
      if (peer == nullptr) return;
    }
    *peer = Peer();
    peer->used = true;
  }
  peer->announce = a;
  peer->emitted = a.emitted;
  peer->seen_at = now;
  peer->has_transport = p.header_type == kHeader2;
  if (peer->has_transport) memcpy(peer->transport_id, p.transport_id, kHashBytes);
  peer->hops = p.hops + 1;
  const uint8_t* dest = a.destination;
  if (we_dial(dest)) {
    // A newer announce while our link to it has gone quiet: it reconnected,
    // that link is dead.
    if (peer->link >= 0 && now - entries_[peer->link].last_in > 3000) {
      close(entries_[peer->link], now);
    }
    if (peer->link < 0 && !pending_for(dest)) {
      peer->dial_at = 0;
      link_to(*peer, now);
    }
  } else if ((fresh || link_count() == 0) && peer->link < 0 &&
             !pending_for(dest)) {
    // It dials us only if it has heard us, and a transport node may have
    // stopped passing our announces on. Give it kFallbackMs; a link that
    // proves the key meanwhile is taken for its.
    peer->fallback_at = now + kFallbackMs;
    peer->accepted_mark = accepted_confirmed_;
  }
}

void ReticulumNode::on_link_request(const Packet& p, int64_t now) {
  if (p.dest_type != kSingle ||
      memcmp(p.destination, destination_, kHashBytes) != 0) {
    return;
  }
  uint8_t id[kHashBytes];
  link_id_of(p.raw, p.raw_len, p.data_len, id);
  // A copy of one already answered, one with no slot it could have, or a
  // flood of requests: no key agreement for it (the first two spend no
  // budget).
  if (find_entry(id) != nullptr || !can_make_room(now) ||
      !request_gate_.allow(now)) {
    return;
  }
  // Only a request a link can come of may cost another its slot.
  Link link;
  size_t proof_len = 0;
  if (!accept(identity_, p, &link, reply_, &proof_len)) return;
  if (!connected_) return;
  if (used_slots() >= kMaxLinks && !evict_link(now)) return;
  add_link(link, nullptr, now);
  write(reply_, proof_len);
}

void ReticulumNode::on_proof(const Packet& p, int64_t now) {
  if (p.dest_type != kLink || p.context != kCtxLrProof) return;
  Pending* pend = find_pending(p.destination);
  if (pend == nullptr) return;
  Link link;
  if (!pend->request.complete(p, &link)) return;
  uint8_t peer_dest[kHashBytes];
  memcpy(peer_dest, pend->request.peer, kHashBytes);
  const int64_t sent_at = pend->sent_at;
  pend->used = false;
  const int idx = add_link(link, peer_dest, now);
  if (idx < 0) return;
  Peer* peer = find_peer(peer_dest);
  if (peer != nullptr) {
    peer->link = idx;
    peer->backoff_ms = 1000;
  }
  Entry& e = entries_[idx];
  write(out_, e.link.rtt_packet((double)(now - sent_at) / 1000.0, out_));
  send_proof(e, now);  // After the RTT, which makes it active at the far end
}

void ReticulumNode::on_data(const Packet& p, int64_t now, Sink* sink) {
  if (p.packet_type != kData || p.dest_type != kLink) return;
  Entry* e = find_entry(p.destination);
  if (e == nullptr) return;
  const bool was_active = e->link.active();
  size_t plain_len = 0, reply_len = 0;
  const Link::Event ev =
      e->link.handle(p, plain_, &plain_len, reply_, &reply_len);
  if (ev == Link::Event::kNone) return;
  e->last_in = now;
  // The far end's RTT: the link is up on our side too.
  if (!was_active && e->link.active()) send_proof(*e, now);
  switch (ev) {
    case Link::Event::kKeepalive:
      if (reply_len > 0) write(reply_, reply_len);
      return;
    case Link::Event::kClose:
      forget(*e, now);
      return;
    case Link::Event::kRtt:
    case Link::Event::kNone:
      return;
    case Link::Event::kData:
      break;
  }
  if (carry::is_key_proof(plain_, plain_len)) {
    on_key_proof(*e, plain_, plain_len, now);
    return;
  }
  // Nothing counts before the far end has proved the key; questions for the
  // boat are not ours.
  if (!e->confirmed || carry::is_ask(plain_, plain_len)) return;
  const uint8_t* whole = nullptr;
  size_t whole_len = 0;
  if (!e->joiner.push(plain_, plain_len, &whole, &whole_len)) return;
  if (!e->allow(now)) return;
  c_.rx++;
  if (sink != nullptr) sink->packet(whole, whole_len);
}

void ReticulumNode::on_key_proof(Entry& e, const uint8_t* payload, size_t len,
                                 int64_t now) {
  if (!e.link.active() ||
      !carry::proof_matches(payload, len, confirm_key_, e.link.id(),
                            !e.link.initiator())) {
    return;
  }
  if (e.confirmed) {
    // It keeps sending its proof, so it has not had ours: once a second at
    // most, again.
    if (now - e.proof_at >= 1000) send_proof(e, now);
    return;
  }
  e.confirmed = true;
  c_.links_up++;
  if (!e.has_peer) accepted_confirmed_++;
}

void ReticulumNode::send_proof(Entry& e, int64_t now) {
  if (!e.link.active()) return;
  uint8_t proof[carry::kProofBytes];
  if (!carry::key_proof(confirm_key_, e.link.id(), e.link.initiator(),
                        proof)) {
    return;  // Tried again on the next resend
  }
  write(out_, e.link.data_packet(proof, sizeof(proof), out_));
  e.proof_at = now;
}

// ---- room in the tables -------------------------------------------------
//
// The name hash we announce under is public, so anyone can announce under it
// (each with an identity of their own) or open links to us. With a hard cap
// alone, a stranger who filled the tables first would keep the crew out. So a
// full table makes room: the link that has waited longest without proving
// the key, the peer heard from longest ago with no confirmed link. A link
// that has proved the key, and the peer behind it, is never the one to go;
// nor is one younger than kGraceMs, since a crew link needs a round trip to
// prove itself.

bool ReticulumNode::can_make_room(int64_t now) const {
  if (used_slots() < kMaxLinks) return true;
  for (const Entry& e : entries_) {
    if (e.used && !e.confirmed && now - e.created_at >= kGraceMs) return true;
  }
  for (const Pending& p : pending_) {
    if (p.used && now - p.sent_at >= kGraceMs) return true;
  }
  return false;
}

bool ReticulumNode::evict_link(int64_t now) {
  Entry* victim = nullptr;
  for (Entry& e : entries_) {
    if (e.used && !e.confirmed && now - e.created_at >= kGraceMs &&
        (victim == nullptr || e.created_at < victim->created_at)) {
      victim = &e;
    }
  }
  if (victim != nullptr) {
    close(*victim, now);
    return true;
  }
  Pending* oldest = nullptr;
  for (Pending& p : pending_) {
    if (p.used && now - p.sent_at >= kGraceMs &&
        (oldest == nullptr || p.sent_at < oldest->sent_at)) {
      oldest = &p;
    }
  }
  if (oldest == nullptr) return false;
  oldest->used = false;
  Peer* peer = find_peer(oldest->request.peer);
  if (peer != nullptr) backoff(*peer, now);
  return true;
}

bool ReticulumNode::evict_peer(int64_t now) {
  Peer* victim = nullptr;
  for (Peer& p : peers_) {
    if (!p.used) continue;
    if (p.link >= 0 && entries_[p.link].confirmed) continue;
    if (victim == nullptr || p.seen_at < victim->seen_at) victim = &p;
  }
  if (victim == nullptr) return false;
  for (Pending& p : pending_) {
    if (p.used &&
        memcmp(p.request.peer, victim->announce.destination, kHashBytes) == 0) {
      p.used = false;
    }
  }
  if (victim->link >= 0) close(entries_[victim->link], now);
  victim->used = false;
  return true;
}

void ReticulumNode::link_to(Peer& peer, int64_t now) {
  if (!connected_ || (used_slots() >= kMaxLinks && !evict_link(now))) return;
  for (Pending& slot : pending_) {
    if (slot.used) continue;
    request(peer.announce,
            peer.hops > 1 && peer.has_transport ? peer.transport_id : nullptr,
            &slot.request);
    slot.used = true;
    slot.sent_at = now;
    write(slot.request.raw, slot.request.raw_len);
    return;
  }
}

int ReticulumNode::add_link(const Link& link, const uint8_t* peer,
                            int64_t now) {
  for (int i = 0; i < kMaxLinks; i++) {
    Entry& e = entries_[i];
    if (e.used) continue;
    e.used = true;
    e.link = link;
    e.has_peer = peer != nullptr;
    if (peer != nullptr) memcpy(e.peer, peer, kHashBytes);
    e.created_at = now;
    e.last_in = now;
    e.confirmed = false;
    e.proof_at = 0;
    e.joiner.reset();
    e.cut_id = 0;
    e.tokens_milli = (int64_t)kLinkBurst * 1000;
    e.refilled = now;
    return i;
  }
  return -1;
}

void ReticulumNode::close(Entry& e, int64_t now) {
  write(out_, e.link.close_packet(out_));
  forget(e, now);
}

void ReticulumNode::forget(Entry& e, int64_t now) {
  if (!e.used) return;
  e.used = false;
  if (e.link.active() && e.confirmed) {
    c_.links_dropped++;
  } else if (!e.confirmed) {
    c_.unproven++;
  }
  if (!e.has_peer) return;
  Peer* peer = find_peer(e.peer);
  if (peer == nullptr) return;
  // A peer we just had a link with is fresher than its last announce says: a
  // flood of strangers' announces must not make it the stalest, and forget
  // it, the moment its link drops.
  peer->seen_at = max64(peer->seen_at, e.last_in);
  if (peer->link == (int)(&e - entries_)) {
    peer->link = -1;
    backoff(*peer, now);
  }
}

// Always on connect: the transport node's path table needs it even when it
// passes it on no further.
void ReticulumNode::announce(int64_t now) {
  uint8_t dest[kHashBytes];
  uint8_t data[kAnnounceMin];
  const size_t n =
      build_announce(identity_, name_hash_, nullptr, 0, wall_s_(), dest, data);
  write(out_, encode(out_, kAnnounce, kSingle, dest, kCtxNone, data, n));
  last_announce_ = now;
  expect_echo_ = budget_.record(now);
}

bool ReticulumNode::tick(int64_t now) {
  if (!connected_) return true;
  // Our last announce went out kStaleMs ago on a connection that echoes
  // them, and nothing at all has come back since: dead, whether or not any
  // link was there to notice.
  if (echoes_ && expect_echo_ && last_rx_ < last_announce_ &&
      now - last_announce_ > kStaleMs) {
    return false;
  }
  // Alone: say so again now and then, while the transport node would still
  // pass it on with some to spare. Linked: nothing.
  if (link_count() == 0 && now - last_announce_ >= kIdleAnnounceMs) {
    const AnnounceBudget::Peek next = budget_.peek(now);
    if (next.passes && next.violations <= AnnounceBudget::kGrace - kRateReserve) {
      announce(now);
    }
  }
  for (Pending& p : pending_) {
    if (!p.used || now - p.sent_at < kLinkTimeoutMs) continue;
    p.used = false;
    c_.unanswered++;
    Peer* peer = find_peer(p.request.peer);
    if (peer != nullptr) backoff(*peer, now);
  }
  int silent = 0;
  for (Entry& e : entries_) {
    if (!e.used) continue;
    const bool stale = now - e.last_in > kStaleMs;
    if (stale || (!e.confirmed && now - e.created_at > kConfirmMs)) {
      if (e.confirmed && stale) silent++;
      close(e, now);
      continue;
    }
    if (!e.confirmed && e.link.active() && now - e.proof_at >= kProofResendMs) {
      send_proof(e, now);
    }
  }
  // Every crew link silent and not a byte from the transport node either:
  // the connection is dead, not the crew.
  if (silent > 0 && link_count() == 0 && now - last_rx_ > kStaleMs) {
    return false;
  }
  for (Peer& peer : peers_) {
    if (!peer.used) continue;
    const uint8_t* dest = peer.announce.destination;
    if (peer.link < 0 && now - peer.seen_at > kPeerForgetMs) {
      peer.used = false;
      continue;
    }
    if (peer.link >= 0 || pending_for(dest)) {
      peer.fallback_at = 0;
      continue;
    }
    if (peer.fallback_at != 0 && now >= peer.fallback_at) {
      peer.fallback_at = 0;
      // Nothing has dialled us since we heard it: it has not heard us, so we
      // dial, now and on every redial.
      if (accepted_confirmed_ == peer.accepted_mark) {
        peer.takeover = true;
        peer.dial_at = 0;
        c_.takeovers++;
      }
    }
    // Redial for a while after it was last heard; after that its next
    // announce does it.
    const bool dialler = we_dial(dest) || peer.takeover;
    if (dialler && now >= peer.dial_at && now - peer.seen_at < kRedialMs) {
      link_to(peer, now);
    }
  }
  return true;
}

// ---- for tests ----------------------------------------------------------

bool ReticulumNode::knows(const uint8_t dest[kHashBytes]) const {
  return find_peer(dest) != nullptr;
}

int64_t ReticulumNode::peer_seen_at(const uint8_t dest[kHashBytes]) const {
  const Peer* p = find_peer(dest);
  return p != nullptr ? p->seen_at : -1;
}

int64_t ReticulumNode::peer_emitted(const uint8_t dest[kHashBytes]) const {
  const Peer* p = find_peer(dest);
  return p != nullptr ? p->emitted : -1;
}

bool ReticulumNode::took_over(const uint8_t dest[kHashBytes]) const {
  const Peer* p = find_peer(dest);
  return p != nullptr && p->takeover;
}

double ReticulumNode::announce_budget_left(int64_t now) {
  return (double)announce_gate_.left(now) / 1000.0;
}

double ReticulumNode::request_budget_left(int64_t now) {
  return (double)request_gate_.left(now) / 1000.0;
}

bool ReticulumNode::holds_link(const uint8_t id[kHashBytes]) const {
  for (const Entry& e : entries_) {
    if (e.used && memcmp(e.link.id(), id, kHashBytes) == 0) return true;
  }
  return false;
}

namespace {
template <typename E>
E* nth_used(E* entries, int count, int i) {
  for (int k = 0; k < count; k++) {
    if (!entries[k].used) continue;
    if (i-- == 0) return &entries[k];
  }
  return nullptr;
}
}  // namespace

const Link* ReticulumNode::entry_link(int i) const {
  const Entry* e = nth_used(entries_, kMaxLinks, i);
  return e != nullptr ? &e->link : nullptr;
}

bool ReticulumNode::entry_confirmed(int i) const {
  const Entry* e = nth_used(entries_, kMaxLinks, i);
  return e != nullptr && e->confirmed;
}

void ReticulumNode::set_entry_confirmed(int i, bool confirmed) {
  Entry* e = nth_used(entries_, kMaxLinks, i);
  if (e != nullptr) e->confirmed = confirmed;
}

void ReticulumNode::set_entry_last_in(int i, int64_t t) {
  Entry* e = nth_used(entries_, kMaxLinks, i);
  if (e != nullptr) e->last_in = t;
}

}  // namespace rns
}  // namespace crewradio
