// SPDX-License-Identifier: EUPL-1.2
#include "lan_peers.h"

#include "packet.h"

namespace crewradio {

void LanPeers::confirm(uint32_t addr, int32_t sender, int64_t now_ms) {
  Peer* stalest = nullptr;
  Peer* free_slot = nullptr;
  for (Peer& p : peers_) {
    if (p.used && p.sender == sender) {
      p.addr = addr;  // Its latest first copy's address
      p.last_ms = now_ms;
      return;
    }
    if (!p.used) {
      if (free_slot == nullptr) free_slot = &p;
    } else if (stalest == nullptr || p.last_ms < stalest->last_ms) {
      stalest = &p;
    }
  }
  Peer* slot = free_slot != nullptr ? free_slot : stalest;
  *slot = {addr, sender, now_ms, true};
}

namespace {
bool listed(const uint32_t* addrs, int n, uint32_t addr) {
  for (int i = 0; i < n; i++) {
    if (addrs[i] == addr) return true;
  }
  return false;
}
}  // namespace

int LanPeers::live_addrs(int64_t now_ms, uint32_t out[kMax]) const {
  int n = 0;
  for (const Peer& p : peers_) {
    if (p.used && now_ms - p.last_ms <= kTtlMs && !listed(out, n, p.addr)) {
      out[n++] = p.addr;
    }
  }
  return n;
}

int LanPeers::live(int64_t now_ms) const {
  uint32_t addrs[kMax];
  return live_addrs(now_ms, addrs);
}

bool BurstFloor::take(int32_t sender, int64_t now_ms) {
  Burst* b = nullptr;
  Burst* free_slot = nullptr;
  Burst* stalest = nullptr;
  for (Burst& x : bursts_) {
    if (x.used && x.sender == sender) {
      b = &x;
      break;
    }
    if (!x.used) {
      if (free_slot == nullptr) free_slot = &x;
    } else if (stalest == nullptr || x.last_audio_ms < stalest->last_audio_ms) {
      stalest = &x;
    }
  }
  bool fresh = false;
  if (b == nullptr) {
    b = free_slot != nullptr ? free_slot : stalest;
    *b = {sender, now_ms, 0, true};
    fresh = true;
  }
  // A new burst (first heard, or after a gap): its first frames keep the
  // group copies.
  if (fresh || now_ms - b->last_audio_ms > kGapMs) b->left = kFrames;
  b->last_audio_ms = now_ms;
  if (b->left > 0) {
    b->left--;
    return true;
  }
  return false;
}

LanPeers::Plan LanPeers::plan(const uint8_t* packet, size_t len,
                              int64_t now_ms, uint32_t except) {
  Plan plan;
  const bool hello = len > 3 && packet[3] == (uint8_t)Codec::kHello;
  bool burst_floor = false;
  if (!hello && len >= packet::kHeader) {
    const int32_t sender =
        (int32_t)(((uint32_t)packet[6] << 24) | ((uint32_t)packet[7] << 16) |
                  ((uint32_t)packet[8] << 8) | (uint32_t)packet[9]);
    burst_floor = floor_.take(sender, now_ms);
  }
  for (const Peer& p : peers_) {
    if (p.used && now_ms - p.last_ms <= kTtlMs && p.addr != except &&
        !listed(plan.addrs, plan.count, p.addr)) {
      plan.addrs[plan.count++] = p.addr;
    }
  }
  plan.group = hello || live(now_ms) == 0 || burst_floor;
  return plan;
}

}  // namespace crewradio
