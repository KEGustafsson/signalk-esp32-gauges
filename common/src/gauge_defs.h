#pragma once

// Gauge pages. A device shows up to kMaxPages pages, in swipe order; each is
// one of three styles (a steel dial, a compass card or the sea horizon) and
// is set up like a SKip gauge widget in the SensESP web UI
// (gauge_settings.h): style or off, title, Signal K path, display unit,
// scale, decimals and data timeout. Zones come from the Signal K server's
// metadata for the path (meta.zones).
//
// This file holds the factory settings only: the pages a freshly flashed
// device shows (default_page(), by its side), and the starting point for a
// page switched to another style (style_template()). Signal K delivers SI
// units (rad, m/s, K, Hz); each definition names a display unit (units.h)
// that converts with `display = raw * scale + offset`.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace skgauges {

enum class GaugeKind : uint8_t {
  kRadial,    // Classic steel dial with needle and LCD readout
  kCompass,   // Rotating compass card with lubber line and LCD heading
  kAttitude,  // Sea-horizon heel/trim indicator (navigation.attitude)
};

// The engine a device's factory pages show (GAUGE_SIDE in platformio.ini).
// Only the defaults depend on it: every page can be repointed in the web UI.
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
  const char* id;        // Short, stable id: the template's, or the page's
  GaugeKind kind;
  const char* title;     // Upper text on the dial face, e.g. "PRT Engine"
  const char* unit_id;   // Default display unit (units.h), e.g. "rpm"
  const char* sk_path;   // Default Signal K path
  float min;             // Default dial range in the default display unit
  float max;
  float major_step;      // A labelled major tick every major_step
  uint8_t minor_per_major;  // Minor tick intervals between majors
  uint8_t decimals;      // Digits after the decimal point on the LCD
  uint16_t listen_delay_ms;  // Signal K subscription period
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

// How many pages a device can have (the web UI's "Number of pages", 1..12).
static constexpr size_t kMaxPages = 12;

// The factory definition of page `slot` (0-based) on a device built for
// `side`, or nullptr past the factory pages (a dial with no path).
const GaugeDef* default_page(Side side, size_t slot);
// How many factory pages there are: a fresh device swipes through these.
size_t factory_page_count();

// Where a page switched to `kind` starts when its factory page is another
// style (or off): a dial reads the path in its SI unit on 0..100, the
// compass a heading, the sea horizon navigation.attitude.
const GaugeDef& style_template(GaugeKind kind);

// Every distinct factory page, both sides (the preview renders these).
extern const GaugeDef* const kFactoryPages[];
extern const size_t kFactoryPageCount;

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
