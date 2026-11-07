// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Wire format, version 4: the app's Packet.kt, byte for byte. 18-byte header,
// big-endian integers, then the sealed payload:
//
//   'P' 'T' | version u8 = 4 | codec u8 | ttl u8 | hops u8 | senderId int32 |
//   seq int32 | time uint32
//   then: nonce (12) | ciphertext | tag (16)
//
// The payload is AES-256-GCM under the packet key (channel_crypto.h) with the
// header as associated data, the ttl byte zeroed (relays rewrite it). `hops`
// is the sender's signed hop budget, `time` its wall clock in seconds; a
// receiver drops a packet more than kReplayWindowS off its own clock before
// any cache is touched (ingress.h). No legacy decoding: a crew runs one wire
// version.

#include <stddef.h>
#include <stdint.h>

namespace crewradio {

enum class Codec : uint8_t {
  kPcm = 0,    // One 20 ms frame of PCM16LE, 16 kHz mono
  kOpus = 1,   // One 20 ms Opus packet
  kHello = 2,  // A roster heartbeat (hello.h), no audio
};

struct Header {
  int32_t sender_id = 0;
  int32_t seq = 0;
  Codec codec = Codec::kPcm;
  int ttl = 0;
  int hops = 0;        // The sender's signed hop budget
  uint32_t time = 0;   // The sender's clock, seconds since the epoch (low 32 bits)
};

namespace packet {

constexpr size_t kHeader = 18;
constexpr uint8_t kVersion = 4;
// Largest packet a peer may send: a sealed PCM frame with its header is 686
// bytes, Opus far less. Anything bigger is dropped unread.
constexpr size_t kMaxSize = 1024;
// How far a packet's time may be from the receiver's clock, either way.
constexpr int kReplayWindowS = 60;

// Writes the 18-byte header; ttl and hops are clamped to a byte.
void encode_header(const Header& h, uint8_t out[kHeader]);

// False for anything that is not a well-formed v4 packet with a non-empty
// payload (and nothing over kMaxSize).
bool parse(const uint8_t* p, size_t len, Header* out);

// True if `time` is within kReplayWindowS of now_s. The difference is taken
// modulo 2^32, so the wire counter's wrap (in 2106) is a small distance.
bool is_fresh(uint32_t time, int64_t now_s);

// The header as authenticated: the first kHeader bytes with the ttl zeroed.
void aad_of(const uint8_t* p, uint8_t out[kHeader]);

// Rewrites the ttl of an encoded packet in place, clamped to a byte.
void set_ttl(uint8_t* p, int ttl);

}  // namespace packet
}  // namespace crewradio
