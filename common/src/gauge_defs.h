#pragma once

// The gauge pages every board can show, in swipe order.
//
// One table drives both the Signal K subscriptions (sk_data.cpp) and the
// rendering (ui/*.cpp), so adding or retuning a gauge is a one-line change in
// gauge_defs.cpp. Signal K delivers SI units (rad, m/s, K, Hz); each entry
// names a display unit (units.h) that converts with
// `display = raw * scale + offset`.
//
// The table holds the defaults only. Like a SKip gauge widget, every page can
// be set up in the SensESP web UI (gauge_settings.h): shown or not, label,
// path, display unit, scale and decimals. Its zones come from the Signal K
// server's metadata for the path (meta.zones), not from this table.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace skgauges {

enum class GaugeKind : uint8_t {
  kRadial,    // Classic steel dial with needle and LCD readout
  kCompass,   // Rotating compass card with lubber line and LCD heading
  kAttitude,  // Sea-horizon heel/trim indicator (navigation.attitude)
};

// Which unit shows a page. Each physical gauge is built for one side
// (GAUGE_SIDE in platformio.ini) and shows that side's engine pages plus the
// shared ones (heading, speed, attitude).
enum class Side : uint8_t {
  kBoth,
  kPort,
  kStarboard,
};

// A coloured band on the dial (e.g. the red zone of a temperature gauge),
// in displayed units.
struct Zone {
  float from;
  float to;
  uint32_t color;  // 0xRRGGBB
};

static constexpr size_t kMaxZones = 8;

struct GaugeDef {
  const char* id;        // Short, stable id; used in the web config path
  GaugeKind kind;
  Side side;
  const char* title;     // Upper text on the dial face, e.g. "PRT Engine"
  const char* unit_id;   // Default display unit (units.h), e.g. "rpm"
  const char* sk_path;   // Default Signal K path
  float min;             // Default dial range in the default display unit
  float max;
  float major_step;      // A labelled major tick every major_step
  uint8_t minor_per_major;  // Minor tick intervals between majors
  uint8_t decimals;      // Digits after the decimal point on the LCD
  uint16_t listen_delay_ms;  // Signal K subscription period
  bool shown_by_default;     // Spare gauges start hidden
  // Fixed bands drawn as part of the scale (only the attitude page's heel
  // bands); a gauge's alarm zones come from Signal K (gauge_resolve.h).
  Zone zones[kMaxZones];
  uint8_t zone_count;

  // Filled in from the display unit by resolve_gauge(); a table entry leaves
  // them at these defaults.
  const char* unit = "";  // Lower text on the dial face, e.g. "rpm"
  float scale = 1.0f;     // display = raw * scale + offset
  float offset = 0.0f;
};

// The page table (gauge_defs.cpp). Entries past kMaxGauges are ignored.
extern const GaugeDef kGauges[];
extern const size_t kGaugeCount;
static constexpr size_t kMaxGauges = 16;

// True if a unit built for `unit_side` shows this page.
inline bool shown_on(const GaugeDef& def, Side unit_side) {
  return def.side == Side::kBoth || unit_side == Side::kBoth ||
         def.side == unit_side;
}

// The side whose pages a unit actually shows and subscribes: its own, or all
// of them if a table edit left it nothing (so it shows and feeds every page
// rather than a blank screen).
inline Side effective_side(Side unit_side) {
  for (size_t i = 0; i < kGaugeCount && i < kMaxGauges; i++) {
    if (shown_on(kGauges[i], unit_side)) return unit_side;
  }
  return Side::kBoth;
}

// The unit label for a page whose Signal K path may have been changed in the
// web UI: a compass fed a magnetic heading must not say "°T".
inline const char* unit_for_path(const GaugeDef& def, const char* sk_path) {
  if (def.kind == GaugeKind::kCompass) {
    return (sk_path != nullptr && strstr(sk_path, "Magnetic") != nullptr)
               ? "\xC2\xB0M"
               : "\xC2\xB0T";
  }
  return def.unit;
}

// A value older than this is shown as dashes: a dial needle parks at its
// minimum, the compass card is dimmed where it stood, and the sea horizon's
// boat symbol levels off, dimmed.
static constexpr uint32_t kStaleAfterMs = 10000;

}  // namespace skgauges
