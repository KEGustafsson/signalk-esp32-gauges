#pragma once

// Turns a page's defaults (gauge_defs.h), its web UI settings and the Signal K
// server's metadata for its path into the definition the page is drawn from
// -- the way SKip's steel gauge combines its widget options with meta:
//  - display unit: the one set in the web UI;
//  - scale: set by hand, else the server's meta.displayScale, else the
//    default (converted to the display unit);
//  - zones: the server's meta.zones for the path, coloured by state as in
//    SKip (normal is not drawn), unless the web UI says to ignore them.
// Host-safe (no Arduino), so the preview runs exactly this code.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

#include "gauge_defs.h"

namespace skgauges {

// A Signal K zone in the path's SI unit; bounds are open when absent.
struct SiZone {
  bool has_lower = false;
  float lower = 0.0f;
  bool has_upper = false;
  float upper = 0.0f;
  uint32_t color = 0;  // From zone_state_color(); 0 = not drawn
};

// What the server's metadata says about the path, in SI units.
struct GaugeMeta {
  bool has_scale = false;  // meta.displayScale with both bounds
  float scale_lower = 0.0f;
  float scale_upper = 0.0f;
  std::vector<SiZone> zones;
  std::string units;  // meta.units, e.g. "Hz"; labels an "SI" (as sent) dial
};

// What the web UI sets (gauge_settings.h). Strings must outlive the result.
struct GaugeOptions {
  const char* title = nullptr;
  const char* sk_path = nullptr;
  const char* unit_id = nullptr;
  bool manual_scale = false;
  float min = 0.0f;
  float max = 0.0f;
  int decimals = -1;  // -1: the default's
  bool show_zones = true;
};

// The colour SKip's dark theme gives a Signal K zone state; 0 for "normal"
// and anything unknown (not drawn).
uint32_t zone_state_color(const char* state);

// A major tick step for [min, max] that divides it evenly into 4..10 steps of
// 1, 2, 2.5 or 5 x 10^n, and the minor ticks per major that suit it.
float nice_step(float min, float max, uint8_t* minor_per_major);

// Widens [*min, *max] to whole multiples of a 1, 2, 2.5 or 5 x 10^n step
// giving about six majors (for a default range converted to another unit,
// e.g. 32..248 F -> 0..250).
void nice_range(float* min, float* max);

// Converts a range given in display unit `from_id` to `to_id` (rounded out to
// a tidy scale), for a page whose unit is changed; "SI" (as sent) counts as
// the other unit's SI unit. Units of different quantities give 0..100.
// Unknown ids leave the range as it is.
void convert_range(const char* from_id, const char* to_id, float* min,
                   float* max);

// The effective definition of `base` with `options` and `meta` applied. The
// result points into all three (an "SI" dial's unit label is meta.units), so
// they must outlive it.
GaugeDef resolve_gauge(const GaugeDef& base, const GaugeOptions& options,
                       const GaugeMeta& meta);

}  // namespace skgauges
