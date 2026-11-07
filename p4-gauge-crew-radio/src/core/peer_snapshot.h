// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Where this node's voice goes, published by the network task (which owns
// the peer table) for the audio task (which sends): the station address,
// the subnet broadcast and the live peers. A seqlock, so neither task waits:
// the writer bumps an odd sequence, writes, bumps it even; a reader that
// sees an odd or changed sequence reads again. Every field is an atomic
// (relaxed, ordered by the sequence's acquire/release), so the copy is free
// of data races in the C++ memory model and lock-free on the P4.

#include <stdint.h>

#include <atomic>

#include "lan_peers.h"

namespace crewradio {

struct PeerView {
  uint32_t ip = 0;  // 0 = no address: send nothing
  uint32_t broadcast = 0;
  int count = 0;
  uint32_t addrs[LanPeers::kMax] = {};
};

class PeerSnapshot {
 public:
  void publish(const PeerView& v) {
    const uint32_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    ip_.store(v.ip, std::memory_order_relaxed);
    broadcast_.store(v.broadcast, std::memory_order_relaxed);
    const int n = v.count < 0 ? 0 : v.count > LanPeers::kMax ? LanPeers::kMax
                                                             : v.count;
    count_.store(n, std::memory_order_relaxed);
    for (int i = 0; i < n; i++) {
      addrs_[i].store(v.addrs[i], std::memory_order_relaxed);
    }
    seq_.store(s + 2, std::memory_order_release);
  }

  // A consistent copy; false only if the writer kept it busy for `tries`.
  bool read(PeerView* out, int tries = 8) const {
    for (int t = 0; t < tries; t++) {
      const uint32_t s1 = seq_.load(std::memory_order_acquire);
      if (s1 & 1) continue;
      out->ip = ip_.load(std::memory_order_relaxed);
      out->broadcast = broadcast_.load(std::memory_order_relaxed);
      out->count = count_.load(std::memory_order_relaxed);
      for (int i = 0; i < out->count; i++) {
        out->addrs[i] = addrs_[i].load(std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == s1) return true;
    }
    return false;
  }

 private:
  std::atomic<uint32_t> seq_{0};
  std::atomic<uint32_t> ip_{0};
  std::atomic<uint32_t> broadcast_{0};
  std::atomic<int> count_{0};
  std::atomic<uint32_t> addrs_[LanPeers::kMax] = {};
};

}  // namespace crewradio
