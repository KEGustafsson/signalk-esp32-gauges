#pragma once

// What a page's web UI card holds and the rules a save of it follows --
// kept free of Arduino and SensESP so they are unit-tested on the host
// (tests/). GaugeSettings (gauge_settings.h) parses the card's JSON into
// PageFields and calls finish_page_save(); the card's schema comes from
// page_schema().

#include <stddef.h>

#include <string>

#include "gauge_defs.h"

namespace skgauges {

// Everything a page is drawn from; nothing comes from the card's position.
struct PageFields {
  GaugeKind kind = GaugeKind::kRadial;
  std::string title;    // Empty: the style's default
  std::string sk_path;  // Empty: a dial that is not subscribed (dashes)
  std::string unit_id = "SI";
  bool manual_scale = false;
  float min = 0.0f;  // A dial's range: by hand, or until the server sends
  float max = 100.0f;  // displayScale
  int decimals = 1;
  bool show_zones = true;
  int timeout_s = (int)(kStaleAfterMs / 1000);
};

constexpr int kMinTimeoutS = 1;
constexpr int kMaxTimeoutS = 600;

// The card of a factory page, or (nullptr) of a page past the factory ones:
// a dial with no path.
PageFields factory_fields(const GaugeDef* factory);

// The web UI's names for the styles: "Dial", "Compass", "Sea horizon".
const char* page_style_name(GaugeKind kind);
bool parse_page_style(const char* name, GaugeKind* kind);

enum class PageCheck {
  kOk,
  kBadUnit,      // Not a display unit (units.h)
  kBadRange,     // Scale minimum not below its maximum, or not finite
  kBadDecimals,  // Outside 0..3
  kBadTimeout,   // Outside kMinTimeoutS..kMaxTimeoutS
};

// A save of a card: `next` is `current` with the values the save carried.
// Applies the save rules, then checks the result; `next` is only usable when
// the check is kOk. With `from_web` (a save in the web UI, not the boot
// load, where the saved style differs from the factory one without anything
// having changed), the fields the save left as they were follow along:
//  - a new style starts from that style's defaults (a heading path left on
//    a sea horizon would never show anything);
//  - a new display unit on a dial that keeps its path converts its range
//    (0..120 °C -> 0..250 °F); a path set in the same save keeps the range,
//    which never described it.
// An empty path becomes the style's usual one (a dial's is empty).
PageCheck finish_page_save(const PageFields& current, PageFields* next,
                           bool from_web);

// The card's JSON schema for SensESP's web UI: the fields of `kind` only (a
// dial's unit, scale and zones are not shown for a compass or sea horizon),
// plus the one-shot actions "Reset to factory settings" and "Swap with
// page" (1..page_count).
std::string page_schema(GaugeKind kind, size_t page_count);

}  // namespace skgauges
