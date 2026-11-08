// SPDX-License-Identifier: EUPL-1.2
#pragma once

// Payload of a Codec::kHello packet, as the app's Hello.kt:
//
//   ver u8 = 2 | transports u8 (bit flags) | ttl u8 | versionCode uint16 BE |
//   nameLen u8 | name UTF-8, at most 32 bytes
//
// `ttl` repeats the hop budget stamped on the header, so a receiver counts
// the hops travelled as ttl - header.ttl. `version_code` is the sender's
// build; this node sends 0 (not applicable), like the Signal K plugin, so the
// phones do not mark it OLD BUILD.

#include <stddef.h>
#include <stdint.h>

#include <string>

namespace crewradio {

struct Hello {
  std::string name;
  uint8_t transports = 0;
  int ttl = 0;
  uint16_t version_code = 0;

  static constexpr uint8_t kVersion = 2;
  static constexpr size_t kMaxNameBytes = 32;
  static constexpr size_t kHead = 6;
  static constexpr size_t kMaxSize = kHead + kMaxNameBytes;

  static constexpr uint8_t kLan = 1;
  static constexpr uint8_t kBluetooth = 2;
  static constexpr uint8_t kAware = 4;
  static constexpr uint8_t kReticulum = 8;
  // crew-radio's "radio under load" bits (forwarder.h): connected to the
  // Reticulum hub now; would rather be the boat's forwarder (the plugin);
  // would rather not be (this node: little RAM). A decoder ignores bits it
  // does not know, so old nodes read these hellos as before.
  static constexpr uint8_t kRnsUp = 0x10;
  static constexpr uint8_t kFwdPrefer = 0x20;
  static constexpr uint8_t kFwdAvoid = 0x40;

  // Writes at most kMaxSize bytes; the name is cut to kMaxNameBytes on a code
  // point boundary. Returns the length written.
  size_t encode(uint8_t* out) const;

  // False for anything off the wire contract: an unknown version, a name over
  // kMaxNameBytes, trailing bytes, invalid UTF-8. The name goes through
  // sanitise_name().
  static bool decode(const uint8_t* p, size_t len, Hello* out);
};

// A name as it may appear on a roster, the rule the app and the plugin
// share: control characters (Cc) and format characters (Cf: bidi overrides,
// zero-width characters, byte order marks, tag characters) dropped, runs of
// white space one space, the ends trimmed. Input must be valid UTF-8.
std::string sanitise_name(const std::string& utf8);

// True if `p` is well-formed UTF-8 (no overlong forms, no surrogates, nothing
// above U+10FFFF): what the app's strict decoder accepts.
bool valid_utf8(const uint8_t* p, size_t len);

// The longest prefix of `utf8` that fits in max_bytes, never cutting a code
// point.
size_t utf8_prefix_len(const std::string& utf8, size_t max_bytes);

}  // namespace crewradio
