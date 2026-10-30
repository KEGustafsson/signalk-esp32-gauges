#include "units.h"

#include <string.h>

namespace skgauges {

const DisplayUnit kDisplayUnits[] = {
    // The value as Signal K sends it, labelled with the server's meta.units
    {"SI", "", "", 1.0f, 0.0f},
    // Revolutions (Hz)
    {"rpm", "rpm", "Hz", 60.0f, 0.0f},
    {"Hz", "Hz", "Hz", 1.0f, 0.0f},
    // Temperature (K)
    {"\xC2\xB0" "C", "\xC2\xB0" "C", "K", 1.0f, -273.15f},
    {"\xC2\xB0" "F", "\xC2\xB0" "F", "K", 1.8f, -459.67f},
    {"K", "K", "K", 1.0f, 0.0f},
    // Speed (m/s)
    {"kn", "kn", "m/s", 1.943844492f, 0.0f},  // 3600 / 1852
    {"km/h", "km/h", "m/s", 3.6f, 0.0f},
    {"mph", "mph", "m/s", 2.236936292f, 0.0f},
    {"m/s", "m/s", "m/s", 1.0f, 0.0f},
    // Angle (rad)
    {"\xC2\xB0", "\xC2\xB0", "rad", 57.29577951308232f, 0.0f},
    // Pressure (Pa)
    {"bar", "bar", "Pa", 1e-5f, 0.0f},
    {"kPa", "kPa", "Pa", 1e-3f, 0.0f},
    {"psi", "psi", "Pa", 1.450377377e-4f, 0.0f},
    // Depth / distance (m)
    {"m", "m", "m", 1.0f, 0.0f},
    {"ft", "ft", "m", 3.280839895f, 0.0f},
    // Ratio (0..1)
    {"%", "%", "ratio", 100.0f, 0.0f},
    // Electrical
    {"V", "V", "V", 1.0f, 0.0f},
    {"A", "A", "A", 1.0f, 0.0f},
    {"W", "W", "W", 1.0f, 0.0f},
};

const size_t kDisplayUnitCount = sizeof(kDisplayUnits) / sizeof(kDisplayUnits[0]);

const DisplayUnit* find_display_unit(const char* id) {
  if (id == nullptr) {
    return nullptr;
  }
  for (size_t i = 0; i < kDisplayUnitCount; i++) {
    if (strcmp(kDisplayUnits[i].id, id) == 0) {
      return &kDisplayUnits[i];
    }
  }
  return nullptr;
}

}  // namespace skgauges
