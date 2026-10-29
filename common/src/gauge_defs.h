#pragma once

// The gauge pages every board can show, in swipe order.
//
// One table drives both the Signal K subscriptions (sk_data.cpp) and the
// rendering (ui/*.cpp), so adding or retuning a gauge is a one-line change in
// gauge_defs.cpp. Signal K delivers SI units (rad, m/s, K, Hz); each entry
// converts to the displayed unit with `display = raw * scale + offset`.
//
// The SK path in the table is only the default: every path can be changed at
// runtime from the SensESP web UI (Configuration -> Gauges) without a rebuild.

#include <stddef.h>
#include <stdint.h>

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

static constexpr size_t kMaxZones = 3;

struct GaugeDef {
  const char* id;        // Short, stable id; used in the web config path
  GaugeKind kind;
  Side side;
  const char* title;     // Upper text on the dial face, e.g. "PRT Engine"
  const char* unit;      // Lower text on the dial face, e.g. "rpm"
  const char* sk_path;   // Default Signal K path
  float scale;           // display = raw * scale + offset
  float offset;
  float min;             // Dial range in displayed units
  float max;
  float major_step;      // A labelled major tick every major_step
  uint8_t minor_per_major;  // Minor tick intervals between majors
  uint8_t decimals;      // Digits after the decimal point on the LCD
  uint16_t listen_delay_ms;  // Signal K subscription period
  Zone zones[kMaxZones];
  uint8_t zone_count;
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

// A value older than this is shown as dashes: a dial needle parks at its
// minimum, the compass card and sea horizon are dimmed where they stood.
static constexpr uint32_t kStaleAfterMs = 10000;

}  // namespace skgauges
