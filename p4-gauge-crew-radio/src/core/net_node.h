// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The network task's whole job, without the sockets: a datagram in, an
// action out. It parses, runs the receive pipeline (ingress.h) with the
// channel key, learns WLAN peers only from fresh first copies (lan_peers.h),
// keeps the roster (roster.h), and hands audio on with the frames lost
// before it. It also builds this node's own hello, once a second. One owner
// (the network task), so no locks; the UI gets roster copies.
//
// The bridge (phase 2b): with Reticulum on as well, a first copy, or one that
// reaches further than the copies before it (Ingress kRelayOnly), goes on to
// the other transport with the ttl Ingress::relay_ttl gives it: from the
// WLAN to Reticulum, from Reticulum to the WLAN. Never back where it came
// from: neither transport relays within itself (the WLAN reaches everyone on
// it at once; on Reticulum every node links to every other). A peer is
// learned only from the WLAN, and a hello heard over Reticulum marks its
// member so (the screen draws it dashed).

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
    bool relay = false;  // Bridge the WLAN and Reticulum
  };
  enum class Via : uint8_t { kLan, kReticulum };
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
    uint64_t relayed = 0;
  };
  struct Audio {
    int32_t sender;
    Codec codec;
    int gap;
    const uint8_t* data;  // Valid until the next on_datagram()
    size_t len;
  };

  // A copy for the other transport, its ttl rewritten.
  struct Relay {
    bool to_lan = false;
    bool to_reticulum = false;
    int32_t sender = 0;
    bool hello = false;
    const uint8_t* data = nullptr;  // Valid until the next packet
    size_t len = 0;
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
                   int64_t now_ms, int64_t now_s, Audio* audio) {
    return on_packet(p, len, Via::kLan, from, now_ms, now_s, audio, nullptr);
  }
  // The same from either transport (`from` only for the WLAN); with relay,
  // *relay says where a copy goes (nowhere unless Config::relay).
  bool on_packet(const uint8_t* p, size_t len, Via via, uint32_t from,
                 int64_t now_ms, int64_t now_s, Audio* audio, Relay* relay);

  // What this node's hellos say it is on (Hello::kLan, kReticulum).
  void set_transports(uint8_t transports) { config_.transports = transports; }

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
  uint8_t relay_[packet::kMaxSize];
};

}  // namespace crewradio
