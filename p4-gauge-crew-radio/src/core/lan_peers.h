// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Who gets a copy of each packet on the WLAN, as the app's LanTransport:
// unicast to every address heard from in the last kTtlMs (at most kMax, the
// stalest evicted), and the multicast group plus the subnet broadcast only
// while no peer is known, for a hello, or for the first kBurstFrames frames
// of a sender's talk burst. An access point sends multicast and broadcast
// at its lowest rate, unacknowledged, so unicast is what carries audio; the
// group copies are how a phone nobody has heard yet is found, and the burst
// floor keeps a table someone has filled with replayed addresses audible.
//
// A peer is learned only from a packet that opened with the channel key and
// that Ingress accepted as a fresh first copy (confirm()): learning from any
// datagram would let any host on the WLAN fill the table and take the audio,
// and learning from a copy or a stale packet would let it do the same by
// replaying one from addresses of its choosing. The table holds one address
// per sender id (authenticated, so only the crew's), the one its latest
// first copy came from: a fresh packet this node missed, replayed from a
// spoofed address, can move only its own sender's entry, which that
// sender's next hello moves back, and never pushes another crew member out.
// Owned by the network task.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {

// The burst floor alone: the first kFrames frames of each sender's talk
// burst (a new burst after kGapMs without audio) keep the group copies.
// LanPeers uses it for relayed audio; the audio task keeps its own for this
// node's voice.
class BurstFloor {
 public:
  static constexpr int64_t kGapMs = 400;
  static constexpr int kFrames = 5;
  static constexpr int kMaxSenders = 32;

  // True if this frame of `sender` keeps its group copies.
  bool take(int32_t sender, int64_t now_ms);

 private:
  struct Burst {
    int32_t sender = 0;
    int64_t last_audio_ms = 0;
    int left = 0;
    bool used = false;
  };
  Burst bursts_[kMaxSenders];
};

class LanPeers {
 public:
  static constexpr int kMax = 16;
  static constexpr int64_t kTtlMs = 5000;
  static constexpr int64_t kBurstGapMs = BurstFloor::kGapMs;
  static constexpr int kBurstFrames = BurstFloor::kFrames;

  struct Plan {
    bool group = false;  // Also to the multicast group and the broadcast address
    int count = 0;
    uint32_t addrs[kMax];  // IPv4, host byte order
  };

  // A fresh first copy from `sender` came from `addr`.
  void confirm(uint32_t addr, int32_t sender, int64_t now_ms);

  // Where `packet` goes; `except` (0 = none) is left out of the unicasts.
  Plan plan(const uint8_t* packet, size_t len, int64_t now_ms,
            uint32_t except = 0);

  // Live addresses (several senders behind one count once).
  int live(int64_t now_ms) const;
  // The live peers' addresses into `out` (kMax), each once; how many.
  int live_addrs(int64_t now_ms, uint32_t out[kMax]) const;

 private:
  struct Peer {
    uint32_t addr = 0;
    int32_t sender = 0;
    int64_t last_ms = 0;
    bool used = false;
  };
  Peer peers_[kMax];
  BurstFloor floor_;
};

}  // namespace crewradio
