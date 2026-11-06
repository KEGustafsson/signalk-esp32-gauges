// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The network task's whole job, without the sockets: a datagram in, an
// action out. It parses, runs the receive pipeline (ingress.h) with the
// channel key, learns WLAN peers only from fresh first copies (lan_peers.h),
// keeps the roster (roster.h), and hands audio on with the frames lost
// before it. It also builds this node's own hello, once a second. One owner
// (the network task), so no locks; the UI gets roster copies.
//
// Only the WLAN in phase 1 and the WLAN does not relay (the app's
// relayWithin = false), so nothing is forwarded; Reticulum (phase 2b) adds
// the bridge with Ingress::relay_ttl.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "audio_config.h"
#include "channel_crypto.h"
#include "hello.h"
#include "ingress.h"
#include "lan_peers.h"
#include "packet.h"
#include "roster.h"

namespace crewradio {

class NetNode {
 public:
  struct Config {
    std::string name;
    int max_hops = audio::kDefaultTtl;
    uint8_t transports = Hello::kLan;
  };
  struct Counters {
    uint64_t received = 0;
    uint64_t malformed = 0;
    uint64_t rejected = 0;
    uint64_t junk_floods = 0;
    uint64_t stale = 0;
    uint64_t duplicates = 0;
    uint64_t late = 0;
    uint64_t hellos = 0;
    uint64_t audio = 0;
  };
  struct Audio {
    int32_t sender;
    Codec codec;
    int gap;
    const uint8_t* data;  // Valid until the next on_datagram()
    size_t len;
  };

  // Largest hello packet: header, hello, nonce and tag.
  static constexpr size_t kMaxHello =
      packet::kHeader + Hello::kMaxSize + ChannelCrypto::kOverhead;

  NetNode(const uint8_t key[ChannelCrypto::kKeyBytes], int32_t self_id,
          const Config& config);

  bool ok() const { return crypto_.ok(); }

  // A datagram from `from` (IPv4, host order). True, with *audio filled,
  // when it carries audio to play.
  bool on_datagram(const uint8_t* p, size_t len, uint32_t from,
                   int64_t now_ms, int64_t now_s, Audio* audio);

  // This node's next hello, sealed: its length, or 0.
  size_t build_hello(int64_t now_s, uint8_t out[kMaxHello]);

  int32_t self_id() const { return self_; }
  LanPeers& peers() { return peers_; }
  Roster& roster() { return roster_; }
  const Counters& counters() const { return c_; }

 private:
  ChannelCrypto crypto_;
  Ingress ingress_;
  Roster roster_;
  LanPeers peers_;
  Config config_;
  int32_t self_;
  int32_t hello_seq_ = 0;
  Counters c_;
  uint8_t plain_[packet::kMaxSize];
};

}  // namespace crewradio
