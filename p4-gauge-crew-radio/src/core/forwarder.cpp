// SPDX-License-Identifier: EUPL-1.2
#include "forwarder.h"

namespace crewradio {

int Forwarder::rank(uint8_t transports) {
  if (transports & kFwdPrefer) return 2;
  if (transports & kFwdAvoid) return 0;
  return 1;
}

void Forwarder::local_hello(int32_t sender, uint8_t transports,
                            int64_t now_ms) {
  Local* slot = nullptr;
  Local* oldest = &local_[0];
  for (Local& l : local_) {
    if (l.used && l.sender == sender) {
      slot = &l;
      break;
    }
    if (slot == nullptr && !l.used) slot = &l;
    if (l.at < oldest->at) oldest = &l;
  }
  // A full table (more than kMaxLocal on one boat): the one heard longest
  // ago makes room; it is the likeliest to have left.
  if (slot == nullptr) slot = oldest;
  slot->used = true;
  slot->sender = sender;
  slot->transports = transports;
  slot->at = now_ms;
}

const Forwarder::Local* Forwarder::find(int32_t sender, int64_t now_ms) const {
  for (const Local& l : local_) {
    if (l.used && l.sender == sender && now_ms - l.at < kLocalMs) return &l;
  }
  return nullptr;
}

bool Forwarder::is_local(int32_t sender, int64_t now_ms) const {
  return find(sender, now_ms) != nullptr;
}

bool Forwarder::local_rns_up(int32_t sender, int64_t now_ms) const {
  const Local* l = find(sender, now_ms);
  return l != nullptr && (l->transports & kRnsUp) != 0;
}

bool Forwarder::forwarder(int32_t self, uint8_t own, int64_t now_ms,
                          int32_t* id) const {
  bool found = false;
  int best_rank = -1;
  uint32_t best_id = 0;
  auto consider = [&](int32_t sender, uint8_t transports) {
    if ((transports & kRnsUp) == 0) return;
    const int r = rank(transports);
    const uint32_t u = (uint32_t)sender;
    if (!found || r > best_rank || (r == best_rank && u < best_id)) {
      found = true;
      best_rank = r;
      best_id = u;
    }
  };
  consider(self, own);
  for (const Local& l : local_) {
    if (l.used && l.sender != self && now_ms - l.at < kLocalMs) {
      consider(l.sender, l.transports);
    }
  }
  if (found) *id = (int32_t)best_id;
  return found;
}

Forwarder::Route Forwarder::audio_route(bool from_lan, bool am_forwarder,
                                        bool talker_rns_up) {
  Route r;
  if (!am_forwarder) return r;
  if (from_lan) {
    r.to_reticulum = !talker_rns_up;
  } else {
    r.to_lan = true;
  }
  return r;
}

void Forwarder::link_heard(uint32_t tag, int32_t sender) {
  const int slot = tag_slot(tag);
  if (tag == 0 || slot < 0 || slot >= kMaxLinks) return;
  links_[slot].tag = tag;
  links_[slot].sender = sender;
}

uint32_t Forwarder::skip_tag(int slot, int64_t now_ms) const {
  if (slot < 0 || slot >= kMaxLinks) return 0;
  const LinkEnd& e = links_[slot];
  if (e.tag == 0 || !is_local(e.sender, now_ms)) return 0;
  return e.tag;
}

}  // namespace crewradio
