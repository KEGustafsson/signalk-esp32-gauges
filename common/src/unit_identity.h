#pragma once

// Which unit a gauge is, decided on the unit rather than at compile time:
// one firmware per board serves every unit of it, so building it once is
// enough. All of it is set in the web UI's Unit card (unit_card.h) and
// saved on the unit:
//  - the role: a starboard or port engine display, or a general one (no
//    engine: navigation, wind, battery); it picks the factory pages and
//    the secrets.h address;
//  - the hostname (SensESP's own setting, /system/hostname);
//  - the display name, shown on the gauge and in the boot log.
// The values here are only what a fresh unit starts with. A unit with no
// role saved (set up before the card had one) takes it from its hostname.
//
// Host-safe (no Arduino, no SensESP), unit-tested in tests/.

#include <string>

#include "gauge_defs.h"

namespace skgauges {

// What a fresh unit's hostname is: "gauge-<board>-stbd" / "-port" / "-gen".
inline std::string unit_hostname(const char* board, Side role) {
  const char* suffix = role == Side::kPort      ? "-port"
                       : role == Side::kGeneral ? "-gen"
                                                : "-stbd";
  return std::string("gauge-") + board + suffix;
}

// True if `hostname` is one of the board's built-in names (it follows the
// role when the role changes; a name of the user's own is kept).
inline bool is_default_hostname(const char* board,
                                const std::string& hostname) {
  for (Side role : {Side::kStarboard, Side::kPort, Side::kGeneral}) {
    if (hostname == unit_hostname(board, role)) return true;
  }
  return false;
}

namespace detail {
inline bool ends_with_ci(const std::string& s, const char* suffix) {
  const std::string tail(suffix);
  if (s.size() < tail.size()) return false;
  for (size_t i = 0; i < tail.size(); i++) {
    char c = s[s.size() - tail.size() + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != tail[i]) return false;
  }
  return true;
}
}  // namespace detail

// The role of a unit with none saved, from its hostname: "-port" is port,
// "-stbd" starboard (any case), anything else general.
inline Side role_from_hostname(const std::string& hostname) {
  if (detail::ends_with_ci(hostname, "-port")) return Side::kPort;
  if (detail::ends_with_ci(hostname, "-stbd")) return Side::kStarboard;
  return Side::kGeneral;
}

// A hostname the network accepts: 1-32 of a-z, 0-9 and '-', not starting
// or ending with '-' (an mDNS / DHCP name).
inline bool valid_hostname(const std::string& hostname) {
  if (hostname.empty() || hostname.size() > 32) return false;
  if (hostname.front() == '-' || hostname.back() == '-') return false;
  for (char c : hostname) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                    c == '-';
    if (!ok) return false;
  }
  return true;
}

// What a fresh unit's display name is, e.g. "2.1in PORT".
inline std::string unit_name(const char* board_label, Side role) {
  const char* suffix = role == Side::kPort      ? " PORT"
                       : role == Side::kGeneral ? " GEN"
                                                : " STBD";
  return std::string(board_label) + suffix;
}

// A display name: 1-24 printable ASCII characters.
inline bool valid_display_name(const std::string& name) {
  if (name.empty() || name.size() > 24) return false;
  for (char c : name) {
    if (c < 0x20 || c > 0x7e) return false;
  }
  return true;
}

// The web UI's Role dropdown: "Starboard" / "Port" / "General".
inline const char* side_label(Side role) {
  return role == Side::kPort      ? "Port"
         : role == Side::kGeneral ? "General"
                                  : "Starboard";
}

// A dropdown value back to a role; false for anything else.
inline bool parse_side_label(const std::string& label, Side* role) {
  for (Side r : {Side::kStarboard, Side::kPort, Side::kGeneral}) {
    if (label == side_label(r)) {
      *role = r;
      return true;
    }
  }
  return false;
}

// What a Unit card save does to the hostname: the one given if it is valid
// and differs; else, if the role changed and the current hostname is still
// a built-in one, the new role's built-in name; else the current one.
inline std::string hostname_after_save(const char* board,
                                       const std::string& current,
                                       Side old_role, Side new_role,
                                       const std::string& given) {
  if (!given.empty() && given != current && valid_hostname(given)) {
    return given;
  }
  if (new_role != old_role && is_default_hostname(board, current)) {
    return unit_hostname(board, new_role);
  }
  return current;
}

}  // namespace skgauges
