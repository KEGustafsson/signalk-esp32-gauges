// SPDX-License-Identifier: EUPL-1.2
// Reticulum nodes on a fake shared medium, as the app's ReticulumNodeTest:
// every packet one writes, the others read, as on one hub segment. Plus the
// plugin's two dead-connection rules, which this node keeps too.
#include <string.h>

#include <deque>
#include <memory>
#include <vector>

#include "core/rns/reticulum_node.h"
#include "test.h"

using namespace crewradio::rns;

namespace {

const uint8_t kCrewKey[32] = {0xb3, 0xe3, 0xa6, 0xeb, 0x30, 0x83, 0xf4, 0x7a,
                              0x06, 0x70, 0xee, 0x3a, 0xb2, 0xd8, 0xe8, 0xdd,
                              0x1f, 0xca, 0x53, 0x92, 0xf2, 0x4c, 0x5b, 0x94,
                              0xe9, 0x5b, 0x16, 0x48, 0xfa, 0x2c, 0x35, 0xe6};
const uint8_t kStrangerKey[32] = {9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
                                  9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9};
const char* kTag = "a2ddc18dee75e2bd";

using Bytes = std::vector<uint8_t>;

struct Medium;

struct TestNode : ReticulumNode::Writer, ReticulumNode::Sink {
  Medium* medium = nullptr;
  std::unique_ptr<ReticulumNode> n;
  std::vector<Bytes> inbox;
  void write(const uint8_t* raw, size_t len) override;
  void packet(const uint8_t* p, size_t len, uint32_t tag) override {
    inbox.emplace_back(p, p + len);
    tags.push_back(tag);
  }
  std::vector<uint32_t> tags;
  ReticulumNode* operator->() { return n.get(); }
};

struct Medium {
  int64_t now = 1000000;
  std::vector<std::unique_ptr<TestNode>> nodes;
  std::deque<std::pair<TestNode*, Bytes>> queue;

  TestNode& node(const char* tag = kTag, const uint8_t* key = kCrewKey) {
    nodes.emplace_back(new TestNode());
    TestNode& t = *nodes.back();
    t.medium = this;
    t.n.reset(new ReticulumNode(tag, key, &t));
    return t;
  }
  void remove(TestNode& t) {
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
      if (it->get() == &t) {
        detached.push_back(std::move(*it));
        nodes.erase(it);
        return;
      }
    }
  }
  // Delivers everything written, and everything written in answer, until
  // the medium is quiet.
  void settle() {
    while (!queue.empty()) {
      auto [from, raw] = queue.front();
      queue.pop_front();
      for (auto& t : nodes) {
        if (t.get() != from) t->n->on_frame(raw.data(), raw.size(), now, t.get());
      }
    }
  }
  // Two nodes, the first the one that dials; linked (and, with the same key,
  // confirmed).
  std::pair<TestNode*, TestNode*> linked(const char* tag = kTag,
                                         const uint8_t* second = kCrewKey) {
    TestNode& x = node(tag);
    TestNode& y = node(tag, second);
    TestNode* a = &x;
    TestNode* b = &y;
    if (memcmp(x->destination(), y->destination(), kHashBytes) > 0) std::swap(a, b);
    (*a)->connected(now);
    (*b)->connected(now);
    settle();
    now += 100;
    (*b)->tick(now);
    (*a)->tick(now);
    settle();
    return {a, b};
  }
  std::vector<std::unique_ptr<TestNode>> detached;
};

void TestNode::write(const uint8_t* raw, size_t len) {
  medium->queue.emplace_back(this, Bytes(raw, raw + len));
}

Bytes bytes(size_t n, uint8_t v) { return Bytes(n, v); }

Bytes announce_of(const Identity& id, const uint8_t* nh, int64_t emitted) {
  uint8_t dest[kHashBytes], data[kAnnounceMin], raw[kMtu];
  const uint8_t zeros[5] = {};
  const size_t n = build_announce(id, nh, nullptr, 0, emitted, dest, data, zeros);
  const size_t len = encode(raw, kAnnounce, kSingle, dest, kCtxNone, data, n);
  return Bytes(raw, raw + len);
}

void feed(TestNode& t, const Bytes& raw, int64_t now) {
  t->on_frame(raw.data(), raw.size(), now, &t);
}

// A link request to `to`, as anyone who has heard its announce can make.
Request request_to(TestNode& to) {
  const Bytes raw = announce_of(to->identity(), to->name_hash(), 1000);
  Packet p;
  decode(raw.data(), raw.size(), &p);
  Announce a;
  CHECK(parse_announce(p, &a));
  Request r;
  request(a, nullptr, &r);
  return r;
}

void feed_request(TestNode& t, const Request& r, int64_t now) {
  t->on_frame(r.raw, r.raw_len, now, &t);
}

void dest_of(const Identity& id, const uint8_t* nh, uint8_t out[kHashBytes]) {
  destination_hash(nh, id.hash(), out);
}

}  // namespace

TEST(rns_node_two_nodes_link_prove_the_key_and_carry_the_channel) {
  Medium m;
  auto [a, b] = m.linked();
  CHECK((*a)->link_count() == 1);
  CHECK((*b)->link_count() == 1);
  const Bytes hello = bytes(54, 1), opus = bytes(88, 2), pcm = bytes(686, 3);
  CHECK((*a)->send(hello.data(), hello.size()));
  CHECK((*a)->send(opus.data(), opus.size()));
  CHECK((*a)->send(pcm.data(), pcm.size()));
  m.settle();
  CHECK(b->inbox.size() == 3);
  CHECK(b->inbox.size() == 3 && b->inbox[0] == hello && b->inbox[1] == opus &&
        b->inbox[2] == pcm);  // PCM arrives whole, in two parts
  CHECK((*b)->send(opus.data(), opus.size()));
  m.settle();
  CHECK(a->inbox.size() == 1);
}

// Each packet names the link it came on, and send() can leave a link out
// (split horizon, core/forwarder.h).
TEST(rns_node_tags_each_link_and_skips_the_ones_asked) {
  Medium m;
  auto [a, b] = m.linked();
  const Bytes opus = bytes(88, 2);
  CHECK((*a)->send(opus.data(), opus.size()));
  m.settle();
  CHECK(b->tags.size() == 1 && b->tags[0] != 0);
  const uint32_t tag = b->tags[0];
  CHECK((tag & 0xFFFFFFu) != 0);  // An epoch, never 0
  CHECK((*b)->send(opus.data(), opus.size()));
  m.settle();
  CHECK(a->tags.size() == 1 && a->tags[0] != 0);
  struct SkipAll : ReticulumNode::Skip {
    uint32_t tag;
    bool skip(uint32_t t) const override { return t == tag; }
  } skip_it;
  skip_it.tag = a->tags[0];  // a's own name for its link to b
  CHECK(!(*a)->send(opus.data(), opus.size(), &skip_it));  // Went nowhere
  m.settle();
  CHECK(b->inbox.size() == 1);
  skip_it.tag = a->tags[0] + 1;  // Another epoch: not this link
  CHECK((*a)->send(opus.data(), opus.size(), &skip_it));
  m.settle();
  CHECK(b->inbox.size() == 2 && b->tags[1] == tag);
  // The links send() would use: none once its only one is skipped (what
  // the hello's kRnsUp claims: a far end off our own WLAN).
  CHECK((*a)->link_count() == 1);
  skip_it.tag = a->tags[0];
  CHECK((*a)->link_count(&skip_it) == 0);
  skip_it.tag = a->tags[0] + 1;
  CHECK((*a)->link_count(&skip_it) == 1);
}

TEST(rns_node_a_stranger_who_copies_the_name_hash_gets_and_passes_nothing) {
  Medium m;
  m.linked(kTag, kStrangerKey);
  TestNode& me = *m.nodes[0];  // The crew's key; nodes[1] another
  TestNode& stranger = *m.nodes[1];
  CHECK(me->entry_count() == 1);
  CHECK(me->link_count() + stranger->link_count() == 0);
  const Bytes hello = bytes(54, 1);
  CHECK(!me->send(hello.data(), hello.size()));
  // A hub that reflects what we send on the link back to us: our own proof.
  m.now += ReticulumNode::kProofResendMs;
  me->tick(m.now);
  std::vector<Bytes> ours;
  while (!m.queue.empty()) {
    if (m.queue.front().first == &me) ours.push_back(m.queue.front().second);
    m.queue.pop_front();
  }
  CHECK(!ours.empty());
  for (const Bytes& raw : ours) feed(me, raw, m.now);
  CHECK(me->link_count() == 0);
  // A genuine channel packet copied from anywhere, on the stranger's link,
  // is dropped unread.
  const Link* link = me->entry_link(0);
  CHECK(link != nullptr);
  uint8_t id[kHashBytes];
  memcpy(id, link->id(), kHashBytes);
  uint8_t parts[carry::kMaxParts][kMdu];
  size_t lens[carry::kMaxParts];
  const size_t n = carry::cut(hello.data(), hello.size(), 0, parts, lens);
  for (size_t i = 0; i < n; i++) {
    uint8_t raw[kMtu];
    const size_t len = link->data_packet(parts[i], lens[i], raw);
    me->on_frame(raw, len, m.now, &me);
  }
  CHECK(me.inbox.empty());
  // The proofs keep the link fresh, so the confirm rule closes it.
  for (int64_t i = 0; i < ReticulumNode::kConfirmMs / ReticulumNode::kProofResendMs; i++) {
    m.now += ReticulumNode::kProofResendMs;
    me->tick(m.now);
    stranger->tick(m.now);
    m.settle();
  }
  m.now += 1000;
  me->tick(m.now);
  m.settle();
  CHECK(!me->holds_link(id));
  CHECK(me->link_count() == 0);
}

TEST(rns_node_a_key_proof_lost_on_the_way_is_sent_again) {
  Medium m;
  TestNode& x = m.node();
  TestNode& y = m.node();
  TestNode* a = &x;
  TestNode* b = &y;
  if (memcmp(x->destination(), y->destination(), kHashBytes) > 0) std::swap(a, b);
  // The first two link data packets with no context are the two ends'
  // proofs (the RTT has its own context): drop them.
  int dropped = 0;
  auto deliver_dropping_proofs = [&]() {
    while (!m.queue.empty()) {
      auto [from, raw] = m.queue.front();
      m.queue.pop_front();
      Packet p;
      if (decode(raw.data(), raw.size(), &p) && p.packet_type == kData &&
          p.dest_type == kLink && p.context == kCtxNone && dropped < 2) {
        dropped++;
        continue;
      }
      for (auto& t : m.nodes) {
        if (t.get() != from) t->n->on_frame(raw.data(), raw.size(), m.now, t.get());
      }
    }
  };
  (*a)->connected(m.now);
  (*b)->connected(m.now);
  deliver_dropping_proofs();
  m.now += 100;
  (*b)->tick(m.now);
  (*a)->tick(m.now);
  deliver_dropping_proofs();
  CHECK(dropped == 2);
  CHECK((*a)->link_count() + (*b)->link_count() == 0);
  for (int i = 0; i < 2; i++) {
    m.now += ReticulumNode::kProofResendMs;
    (*a)->tick(m.now);
    (*b)->tick(m.now);
    m.settle();
  }
  CHECK((*a)->link_count() == 1);
  CHECK((*b)->link_count() == 1);
}

TEST(rns_node_another_channel_is_never_dialled_and_an_older_announce_changes_nothing) {
  Medium m;
  TestNode& mine = m.node("1111111111111111");
  TestNode& other = m.node("2222222222222222");
  mine->connected(m.now);
  other->connected(m.now);
  m.settle();
  CHECK(mine->peer_count() == 0);
  CHECK(other->peer_count() == 0);
  const Identity peer = Identity::generate();
  feed(mine, announce_of(peer, mine->name_hash(), 2000), m.now);
  feed(mine, announce_of(peer, mine->name_hash(), 1000), m.now);
  CHECK(mine->peer_count() == 1);
  uint8_t dest[kHashBytes];
  dest_of(peer, mine->name_hash(), dest);
  CHECK(mine->peer_emitted(dest) == 2000);
  CHECK(mine->counters().older == 1);
  // Our own announce, echoed back, is not a peer.
  feed(mine, announce_of(mine->identity(), mine->name_hash(), 3000), m.now);
  CHECK(mine->peer_count() == 1);
  CHECK(mine->counters().echoes == 1);
  const Bytes junk = {1, 2, 3};
  feed(mine, junk, m.now);
}

TEST(rns_node_silent_links_close_the_dialler_redials_and_disconnect_clears) {
  Medium m;
  auto [a, b] = m.linked();
  CHECK((*a)->link_count() == 1);
  m.now += ReticulumNode::kStaleMs + 1;
  (*a)->tick(m.now);
  m.settle();
  CHECK((*a)->entry_count() == 0);
  CHECK((*b)->entry_count() == 0);  // The close reached the far end
  m.now += 2000;
  (*a)->tick(m.now);
  m.settle();
  CHECK((*a)->link_count() == 1);  // Redialled after the backoff
  m.remove(*b);  // b goes away: the link falls silent, requests unanswered
  m.now += ReticulumNode::kStaleMs + 1;
  (*a)->tick(m.now);
  m.queue.clear();
  CHECK((*a)->entry_count() == 0);
  m.now += 20000;
  (*a)->tick(m.now);
  m.queue.clear();
  CHECK((*a)->pending_count() == 1);  // A redial waiting
  m.now += ReticulumNode::kLinkTimeoutMs + 1;
  (*a)->tick(m.now);
  m.queue.clear();
  CHECK((*a)->pending_count() == 0);  // Given up
  m.now += 20000;
  (*a)->tick(m.now);
  m.queue.clear();
  (*a)->disconnected(m.now);
  CHECK((*a)->pending_count() == 0);
  CHECK((*a)->entry_count() == 0);
  const Bytes some = bytes(40, 0);
  CHECK(!(*a)->send(some.data(), some.size()));
}

TEST(rns_node_a_full_peer_table_never_forgets_a_confirmed_peer) {
  Medium m;
  auto [a, b] = m.linked("5555555555555555");
  CHECK((*a)->link_count() == 1);
  m.queue.clear();
  uint8_t b_dest[kHashBytes];
  memcpy(b_dest, (*b)->destination(), kHashBytes);
  m.remove(*b);
  // Strangers announcing under our public name hash fill the table; each new
  // one still gets in.
  const Identity first = Identity::generate();
  m.now += 100;
  feed(*a, announce_of(first, (*a)->name_hash(), m.now / 1000), m.now);
  for (int i = 0; i < ReticulumNode::kMaxPeers; i++) {
    m.now += 100;
    feed(*a, announce_of(Identity::generate(), (*a)->name_hash(), m.now / 1000), m.now);
    m.queue.clear();
  }
  CHECK((*a)->peer_count() == ReticulumNode::kMaxPeers);
  uint8_t first_dest[kHashBytes];
  dest_of(first, (*a)->name_hash(), first_dest);
  CHECK(!(*a)->knows(first_dest));  // The stalest made room
  CHECK((*a)->knows(b_dest));       // Never the peer behind a confirmed link
  // Its link drops: it is as fresh as that link was, not as its announce.
  const int64_t last_heard = m.now;
  for (int i = 0; i < (*a)->entry_count(); i++) {
    if ((*a)->entry_confirmed(i)) (*a)->set_entry_last_in(i, last_heard);
  }
  m.now += ReticulumNode::kStaleMs + 1;
  (*a)->tick(m.now);
  m.queue.clear();
  CHECK((*a)->peer_seen_at(b_dest) == last_heard);
  m.now += 100;
  feed(*a, announce_of(Identity::generate(), (*a)->name_hash(), m.now / 1000), m.now);
  m.queue.clear();
  CHECK((*a)->knows(b_dest));  // Strangers heard before its last link go first
}

TEST(rns_node_a_full_link_table_makes_room_only_past_the_grace) {
  Medium m;
  TestNode& me = m.node("6666666666666666");
  me->connected(m.now);
  m.queue.clear();
  std::vector<Bytes> ids;
  for (int i = 0; i < ReticulumNode::kMaxLinks; i++) {
    m.now += 100;  // Within the request budget (10 a second)
    const Request r = request_to(me);
    feed_request(me, r, m.now);
    ids.emplace_back(r.id, r.id + kHashBytes);
  }
  m.queue.clear();
  CHECK(me->entry_count() == ReticulumNode::kMaxLinks);
  for (const Bytes& id : ids) CHECK(me->holds_link(id.data()));
  // A request no link can come of (65 bytes of data) takes nobody's slot.
  m.now += 100;
  {
    Request r = request_to(me);
    Bytes raw(r.raw, r.raw + r.raw_len);
    raw.push_back(0);
    feed(me, raw, m.now);
  }
  for (const Bytes& id : ids) CHECK(me->holds_link(id.data()));
  // Every link younger than the grace: a new request is refused.
  m.now += 100;
  const Request early = request_to(me);
  feed_request(me, early, m.now);
  CHECK(!me->holds_link(early.id));
  // Past the grace, the oldest unconfirmed link makes room.
  m.now += ReticulumNode::kGraceMs;
  const Request late = request_to(me);
  feed_request(me, late, m.now);
  m.queue.clear();
  CHECK(me->holds_link(late.id));
  CHECK(!me->holds_link(ids[0].data()));
  CHECK(me->entry_count() == ReticulumNode::kMaxLinks);
  // Once every link has proved the key, none is ever the one to go.
  for (int i = 0; i < me->entry_count(); i++) me->set_entry_confirmed(i, true);
  m.now += ReticulumNode::kGraceMs;
  const Request after = request_to(me);
  feed_request(me, after, m.now);
  CHECK(!me->holds_link(after.id));
  CHECK(me->holds_link(late.id));
}

TEST(rns_node_floods_past_the_budget_cost_no_signature_work) {
  Medium m;
  TestNode& me = m.node("7777777777777777");
  me->connected(m.now);
  m.queue.clear();
  for (int i = 0; i < ReticulumNode::kMaxLinks; i++) feed_request(me, request_to(me), m.now);
  m.queue.clear();
  CHECK(me->entry_count() == ReticulumNode::kGateBurst);
  for (int i = 0; i < 2 * ReticulumNode::kGateBurst; i++) {
    feed(me, announce_of(Identity::generate(), me->name_hash(), m.now / 1000), m.now);
    m.queue.clear();
  }
  CHECK(me->peer_count() == ReticulumNode::kGateBurst);
  m.now += 1000;
  feed(me, announce_of(Identity::generate(), me->name_hash(), m.now / 1000), m.now);
  m.queue.clear();
  CHECK(me->peer_count() == ReticulumNode::kGateBurst + 1);  // It refills
}

TEST(rns_node_copies_of_a_checked_announce_and_slotless_requests_spend_nothing) {
  Medium m;
  TestNode& me = m.node("9999999999999999");
  me->connected(m.now);
  m.queue.clear();
  const Bytes one = announce_of(Identity::generate(), me->name_hash(), m.now / 1000);
  for (int i = 0; i < 50; i++) feed(me, one, m.now);
  m.queue.clear();
  CHECK(me->peer_count() == 1);
  CHECK_NEAR(me->announce_budget_left(m.now), ReticulumNode::kGateBurst - 1.0, 0.001);
  TestNode& full = m.node("aaaaaaaaaaaaaaaa");
  full->connected(m.now);
  m.queue.clear();
  for (int i = 0; i < ReticulumNode::kMaxLinks; i++) {
    m.now += 100;
    feed_request(full, request_to(full), m.now);
  }
  m.queue.clear();
  CHECK(full->entry_count() == ReticulumNode::kMaxLinks);
  const double before = full->request_budget_left(m.now);
  for (int i = 0; i < 40; i++) feed_request(full, request_to(full), m.now);
  CHECK_NEAR(full->request_budget_left(m.now), before, 0.001);
}

TEST(rns_node_requests_nobody_answers_never_lock_the_crew_out) {
  Medium m;
  TestNode& me = m.node("8888888888888888");
  me->connected(m.now);
  m.queue.clear();
  // Strangers announcing under our name with destinations we are to dial:
  // each costs a pending request of ours.
  while (me->pending_count() < ReticulumNode::kMaxLinks) {
    const Identity id = Identity::generate();
    uint8_t dest[kHashBytes];
    dest_of(id, me->name_hash(), dest);
    if (memcmp(me->destination(), dest, kHashBytes) > 0) continue;  // It dials us
    m.now += 100;
    feed(me, announce_of(id, me->name_hash(), m.now / 1000), m.now);
  }
  m.queue.clear();
  CHECK(me->entry_count() == 0);
  m.now += ReticulumNode::kGraceMs;
  const Request crew = request_to(me);
  feed_request(me, crew, m.now);
  CHECK(me->entry_count() == 1 && me->holds_link(crew.id));
  CHECK(me->pending_count() == ReticulumNode::kMaxLinks - 1);
}

TEST(rns_node_a_confirmed_link_is_held_to_its_own_rate_budget) {
  Medium m;
  auto [a, b] = m.linked();
  const Bytes opus = bytes(88, 1);
  for (int i = 0; i < 1000; i++) (*a)->send(opus.data(), opus.size());
  m.settle();
  CHECK(b->inbox.size() == 800);  // A burst of 800, then nothing until it refills
  m.now += 1000;
  (*a)->send(opus.data(), opus.size());
  m.settle();
  CHECK(b->inbox.size() == 801);
}

TEST(rns_node_questions_for_the_boat_are_nobodys_channel_packet) {
  Medium m;
  auto [a, b] = m.linked();
  const uint8_t question[] = {0x81, 0x12, 0x34, 0, 1, 1, 'n', 'a', 'v'};
  const Link* link = (*a)->entry_link(0);
  CHECK(link != nullptr);
  uint8_t raw[kMtu];
  const size_t len = link->data_packet(question, sizeof(question), raw);
  (*b)->on_frame(raw, len, m.now, b);
  CHECK(b->inbox.empty());
}

TEST(rns_node_announces_on_connect_again_while_alone_never_while_linked) {
  Medium m;
  TestNode& alone = m.node("3535353535353535");
  alone->connected(m.now);
  m.queue.clear();
  const int64_t first = alone->announced_at();
  m.now += ReticulumNode::kIdleAnnounceMs - 1000;
  alone->tick(m.now);
  CHECK(alone->announced_at() == first);  // Not yet
  std::vector<bool> made;
  for (int i = 0; i < 5; i++) {
    m.now += 1000;
    alone->tick(m.now);
    made.push_back(alone->announced_at() == m.now);
    m.now += ReticulumNode::kIdleAnnounceMs - 1000;
  }
  m.queue.clear();
  // Three more, and two left for reconnects.
  CHECK(made == std::vector<bool>({true, true, true, false, false}));
  CHECK(alone->announce_violations() == 3);

  Medium m2;
  auto [a, b] = m2.linked("3636363636363636");
  const int64_t a_at = (*a)->announced_at();
  const int64_t b_at = (*b)->announced_at();
  for (int64_t i = 0; i <= ReticulumNode::kIdleAnnounceMs / 10000; i++) {
    m2.now += 10000;
    const Bytes hello = bytes(54, (uint8_t)i);
    (*a)->send(hello.data(), hello.size());
    (*b)->send(hello.data(), hello.size());
    m2.settle();
    (*a)->tick(m2.now);
    (*b)->tick(m2.now);
    m2.settle();
  }
  CHECK((*a)->link_count() == 1);
  CHECK((*a)->announced_at() == a_at);
  CHECK((*b)->announced_at() == b_at);
}

TEST(rns_node_a_newcomer_that_has_not_heard_us_is_dialled_after_the_fallback) {
  Medium m;
  TestNode& x = m.node("3a3a3a3a3a3a3a3a");
  TestNode& y = m.node("3a3a3a3a3a3a3a3a");
  TestNode* a = &x;
  TestNode* b = &y;
  if (memcmp(x->destination(), y->destination(), kHashBytes) > 0) std::swap(a, b);
  (*b)->connected(m.now);
  m.queue.clear();  // b was there first: its announce reached nobody
  (*a)->connected(m.now);
  m.settle();
  CHECK(!(*a)->knows((*b)->destination()));
  CHECK((*b)->knows((*a)->destination()));
  m.now += ReticulumNode::kFallbackMs - 1;
  (*b)->tick(m.now);
  m.settle();
  CHECK((*b)->link_count() == 0);  // Not yet: a might still dial
  m.now += 1;
  (*b)->tick(m.now);
  m.settle();
  CHECK((*b)->link_count() == 1);  // b dialled a instead
  CHECK((*a)->link_count() == 1);
  CHECK((*b)->took_over((*a)->destination()));
  // The connection drops and comes back (both announces lost): b redials.
  (*a)->disconnected(m.now);
  (*b)->disconnected(m.now);
  (*a)->connected(m.now);
  (*b)->connected(m.now);
  m.queue.clear();
  m.now += 1000;
  (*b)->tick(m.now);
  m.settle();
  CHECK((*b)->link_count() == 1);

  // d is alone and c comes; this time d's next announce reaches c, which
  // dials d itself.
  Medium m2;
  TestNode& p = m2.node("3b3b3b3b3b3b3b3b");
  TestNode& q = m2.node("3b3b3b3b3b3b3b3b");
  TestNode* c = &p;
  TestNode* d = &q;
  if (memcmp(p->destination(), q->destination(), kHashBytes) > 0) std::swap(c, d);
  (*d)->connected(m2.now);
  m2.queue.clear();
  (*c)->connected(m2.now);
  m2.settle();  // d heard c
  (*d)->disconnected(m2.now);
  (*d)->connected(m2.now);
  m2.settle();  // d's reconnect announce reaches c
  CHECK((*d)->link_count() == 1);  // c dialled d
  m2.now += ReticulumNode::kFallbackMs;
  (*d)->tick(m2.now);
  m2.settle();
  CHECK((*d)->entry_count() == 1);  // The link that came is taken for c's
  CHECK(!(*d)->took_over((*c)->destination()));
}

TEST(rns_node_a_dropped_connection_keeps_linked_peers_fresh_for_a_redial) {
  Medium m;
  auto [a, b] = m.linked("3737373737373737");
  CHECK((*a)->link_count() == 1);
  const int64_t heard = (*a)->peer_seen_at((*b)->destination());
  for (int64_t i = 0; i <= (ReticulumNode::kRedialMs + 60000) / 10000; i++) {
    m.now += 10000;
    const Bytes hello = bytes(54, (uint8_t)i);
    (*b)->send(hello.data(), hello.size());
    m.settle();
    (*a)->tick(m.now);
    (*b)->tick(m.now);
    m.settle();
  }
  (*a)->disconnected(m.now);
  (*b)->disconnected(m.now);
  m.queue.clear();
  CHECK((*a)->peer_seen_at((*b)->destination()) > heard + ReticulumNode::kRedialMs);
  (*a)->connected(m.now);
  (*b)->connected(m.now);
  m.queue.clear();  // The announces of reconnecting, lost
  m.now += 1000;
  (*a)->tick(m.now);
  m.settle();
  CHECK((*a)->link_count() == 1);  // Redialled without waiting
}

TEST(rns_node_a_connection_silent_under_its_links_is_dead) {
  Medium m;
  auto [a, b] = m.linked("3838383838383838");
  CHECK((*a)->link_count() == 1);
  m.remove(*b);  // Nothing comes from the transport node any more
  m.now += ReticulumNode::kStaleMs + 1;
  CHECK(!(*a)->tick(m.now));
  m.queue.clear();
  // One crew link silent while the transport node still sends something is
  // the crew, not the connection.
  Medium m2;
  auto [c, d] = m2.linked("3939393939393939");
  m2.remove(*d);
  m2.now += ReticulumNode::kStaleMs - 1000;
  const Bytes junk = {1, 2, 3};
  feed(*c, junk, m2.now);
  m2.now += 1001 + 1000;
  CHECK((*c)->tick(m2.now));
  CHECK((*c)->link_count() == 0);
}

TEST(rns_node_an_announce_not_echoed_on_a_connection_that_echoes_is_dead) {
  Medium m;
  TestNode& me = m.node("3c3c3c3c3c3c3c3c");
  me->connected(m.now);
  // The transport node sends our announce back: the connection echoes.
  const Bytes own = m.queue.front().second;
  m.queue.clear();
  m.now += 500;
  feed(me, own, m.now);
  CHECK(me->counters().echoes == 1);
  m.now += ReticulumNode::kIdleAnnounceMs;
  CHECK(me->tick(m.now));
  CHECK(me->announced_at() == m.now);  // Alone: announced again
  m.queue.clear();
  m.now += ReticulumNode::kStaleMs + 1;
  CHECK(!me->tick(m.now));  // And no echo, nor anything else, came

  // A connection that never echoed is not judged by it.
  Medium m2;
  TestNode& other = m2.node("3d3d3d3d3d3d3d3d");
  other->connected(m2.now);
  m2.queue.clear();
  m2.now += ReticulumNode::kIdleAnnounceMs;
  CHECK(other->tick(m2.now));
  m2.queue.clear();
  m2.now += ReticulumNode::kStaleMs + 1;
  CHECK(other->tick(m2.now));
}
