// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The channel over Reticulum on the 4" (phase 2b): one TCP connection to a
// Reticulum transport node (the boat's rnsd, or a hub ashore), and the
// links inside it, as the app's ReticulumTransport and the plugin's
// lib/rns/transport.js. core/rns/reticulum_node.h has the protocol; this is
// the socket and the task around it.
//
//   cr-rns (core 0, priority 5: below cr-net and cr-audio) owns the socket
//   and the node: resolves the node's name, connects (10 s, cut short when
//   the radio stops), reads frames, ticks the node once a second, and writes
//   what the other tasks hand it. Its signature work (an announce or a link,
//   some tens of milliseconds on the P4) never holds up the audio, which is
//   why it is a task of its own rather than part of cr-net.
//
// Two ring buffers in PSRAM carry the channel's sealed packets: in, to cr-net
// (its Ingress is the only one, so every packet still passes the one receive
// pipeline), and out, from cr-net (hellos, the bridge) and cr-audio (this
// node's voice). A packet that finds a ring full is dropped and counted.
//
// The connection is opened only while the clock is trusted and the memory
// guard leaves Reticulum on (the first thing it sheds), and re-opened after
// 1 s doubling to 15 s, reset once a connection has held 30 s. A connection
// that dies without a word is noticed by the node's silence rules; TCP
// keep-alives and a 2 s send time-out do the rest. cr-rns is not under the
// Supervisor: its waits are a DNS lookup and a connect, longer than the
// Supervisor's 2 s by design, and each is bounded. It leaves when the radio
// stops for good, closing its links first.

#if defined(ESP_PLATFORM)

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace crewradio {
namespace esp {

struct ReticulumStatus {
  enum class State : uint8_t {
    kOff,         // Not switched on in the card
    kWaiting,     // For the clock, or the memory guard
    kConnecting,  // Or waiting to try again (`note` says why)
    kConnected,
  };
  State state = State::kOff;
  std::string where;  // host:port, an IPv6 address in brackets
  std::string note;   // The last thing that went wrong, short
  int links = 0;      // Confirmed
  int peers = 0;
  uint32_t reconnects = 0;
  uint32_t tx = 0, rx = 0;           // Channel packets
  uint32_t dropped_in = 0, dropped_out = 0;  // Rings full
  uint32_t announces = 0, takeovers = 0, unproven = 0;
};

namespace reticulum {

struct Config {
  std::string host;
  uint16_t port = 4242;
  char tag[17] = {};          // ChannelCrypto::reticulum_tag
  uint8_t confirm_key[32] = {};  // ChannelCrypto::reticulum_confirm_key
  bool (*may_connect)() = nullptr;  // Clock trusted, memory guard allows
  bool (*stopped)() = nullptr;      // The radio is off for good
};

// Creates the rings and cr-rns; false if they could not be (the radio then
// runs on the WLAN alone).
bool start(const Config& config);

// A sealed channel packet for every confirmed link (any task). False when
// there is no link to take it, or the ring is full.
bool send(const uint8_t* packet, size_t len);
// One sealed channel packet that came over Reticulum, into out (cap bytes):
// its length, or 0 when none is waiting. cr-net only.
size_t receive(uint8_t* out, size_t cap);
// Connected to the transport node: what this node's hellos may claim.
bool connected();
// At least one confirmed link.
bool linked();
ReticulumStatus status();

}  // namespace reticulum
}  // namespace esp
}  // namespace crewradio

#endif
