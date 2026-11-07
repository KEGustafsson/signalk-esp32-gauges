// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The radio's place in the gauge firmware, through SensESP: a "Crew radio"
// card in the web UI's Configuration (on/off, name, channel key, group,
// port, hop limit, volume, and for talking the button's GPIO, the
// two-finger hold, Opus or PCM, mic gain and channel, key beeps; takes
// effect at the next restart), its lines on
// the Status page, and the boat's GNSS time from Signal K
// (navigation.datetime) for the clock. Off by default: a gauge that never
// has the radio switched on runs exactly as before.
//
// The channel key itself is never stored: a save derives the packet key
// (PBKDF2, a few seconds; the Save button waits) and keeps only that, in
// NVS, and the card keeps the last four characters to show which key it is.

#if defined(ESP_PLATFORM)

namespace crewradio {
namespace glue {

// From setup(), after gauge_app_begin() (SensESP is up) and the board (the
// I2C driver is installed). `finger_count` says how many fingers are on the
// screen (the gauges' touch port, lvgl_port.h); nullptr for no touch talk.
void begin(int (*finger_count)());

}  // namespace glue
}  // namespace crewradio

#endif
