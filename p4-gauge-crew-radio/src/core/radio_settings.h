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

// How the card shows a stored key: "•••• •••• " and its last four, or
// nothing. The key itself is never shown nor stored: only the packet key
// derived from it is kept (in NVS).
std::string mask_key(const std::string& key);

}  // namespace settings
}  // namespace crewradio
