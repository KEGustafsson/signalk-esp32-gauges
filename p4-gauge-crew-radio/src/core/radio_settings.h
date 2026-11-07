// SPDX-License-Identifier: EUPL-1.2
#pragma once

// What the radio's settings card accepts, as the app's SettingsRules: the
// same channel key rule (12-64 printable ASCII, no space at either end:
// the plugin trims its key and the app does not, so a stray space derives a
// different key on one side and the node silently never appears), the same
// multicast group and port rules, and how the key is shown (never itself).

#include <stdint.h>

#include <string>

namespace crewradio {
namespace settings {

constexpr int kKeyMin = 12;
constexpr int kKeyMax = 64;
constexpr const char* kDefaultGroup = "239.255.42.1";
constexpr int kDefaultPort = 47474;
constexpr int kMaxHopsMin = 1;
constexpr int kMaxHopsMax = 8;
constexpr int kNameMax = 32;  // Hello::kMaxNameBytes

bool valid_channel_key(const std::string& key);

// A dotted IPv4 multicast address (224-239.x.x.x, plain decimal octets):
// the group in host byte order.
bool parse_group(const std::string& text, uint32_t* group);

bool valid_port(int port);

// A GPIO for the talk button: -1 (none), or one of the ESP32-P4's 0-54 that
// the 4" does not already use (touch I2C 7/8, I2S 9-13, backlight 26, LCD
// reset 27, microSD 39-44, amplifier enable 53, USB 24/25).
bool valid_talk_gpio(int gpio);

// The port of a Reticulum transport node's TCP server interface when none
// is given.
constexpr int kDefaultReticulumPort = 4242;

// A Reticulum transport node as the app's SettingsRules.parseHostPort takes
// it: `host`, `host:port`, `[v6]` or `[v6]:port`, the host a name or an
// address (letters, digits, dots, hyphens; colons only inside brackets or as
// a bare IPv6 address), the port 1-65535 written plainly, 4242 when left
// out, white space at the ends ignored. An IPv6 address must be one (RFC
// 4291's text forms: eight groups, one "::" at most, an IPv4 tail), which is
// stricter than the app: "1.2.3.4:5:6" or "::::" never reach the resolver.
// False for anything else.
bool parse_host_port(const std::string& text, std::string* host, int* port);

// How the card shows a stored key: "•••• •••• " and its last four, or
// nothing. The key itself is never shown nor stored: only the packet key
// derived from it is kept (in NVS).
std::string mask_key(const std::string& key);

}  // namespace settings
}  // namespace crewradio
