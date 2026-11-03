#pragma once

// Which unit a gauge is, decided on the unit rather than at compile time:
// one firmware per board serves both sides, so building it once is enough
// for every unit of that board. The identity is SensESP's hostname, saved
// on the unit (/system/hostname, editable in its web UI): one ending in
// "-port" is a port unit, anything else starboard. A fresh unit starts as
// its board's starboard name.
//
// Host-safe (no Arduino, no SensESP), unit-tested in tests/.

#include <string>

#include "gauge_defs.h"

namespace skgauges {

// "gauge-<board>-stbd" / "-port", e.g. board "21" -> "gauge-21-port".
inline std::string unit_hostname(const char* board, Side side) {
  return std::string("gauge-") + board +
         (side == Side::kPort ? "-port" : "-stbd");
}

// A port unit's hostname ends in "-port" (any case); anything else,
// a customised name included, is starboard.
inline Side side_from_hostname(const std::string& hostname) {
  static const char kSuffix[] = "-port";
  const size_t n = sizeof(kSuffix) - 1;
  if (hostname.size() < n) {
    return Side::kStarboard;
  }
  for (size_t i = 0; i < n; i++) {
    char c = hostname[hostname.size() - n + i];
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    if (c != kSuffix[i]) {
      return Side::kStarboard;
    }
  }
  return Side::kPort;
}

// The web UI's Unit dropdown: "Starboard" / "Port".
inline const char* side_label(Side side) {
  return side == Side::kPort ? "Port" : "Starboard";
}

// A dropdown value back to a side; false for anything else.
inline bool parse_side_label(const std::string& label, Side* side) {
  if (label == "Port") {
    *side = Side::kPort;
    return true;
  }
  if (label == "Starboard") {
    *side = Side::kStarboard;
    return true;
  }
  return false;
}

// The label shown in the info overlay, e.g. "2.1in PORT".
inline std::string unit_name(const char* board_label, Side side) {
  return std::string(board_label) +
         (side == Side::kPort ? " PORT" : " STBD");
}

}  // namespace skgauges
