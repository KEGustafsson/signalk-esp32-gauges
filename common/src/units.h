#pragma once

// Display units a gauge can be set to (web UI, like SKip's "convert to"),
// each converting from the Signal K SI unit of the path:
// display = si * scale + offset. Host-safe: no Arduino, used by the preview.

#include <stddef.h>

namespace skgauges {

struct DisplayUnit {
  const char* id;     // Saved in the settings and shown in the web UI's
                      // unit list, e.g. "rpm", "\xC2\xB0" "C"
  const char* label;  // Shown on the dial, e.g. "rpm", "\xC2\xB0" "C"
  const char* si;     // Signal K unit it converts from, e.g. "Hz"
  float scale;
  float offset;
};

extern const DisplayUnit kDisplayUnits[];
extern const size_t kDisplayUnitCount;

// The unit with this id, or nullptr.
const DisplayUnit* find_display_unit(const char* id);

}  // namespace skgauges
