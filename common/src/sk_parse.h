#pragma once

// How a Signal K delta's value is read: only a real JSON number counts.
// ArduinoJson's as<float>() coerces null, a string or an object to 0.0f --
// for a gauge that is a confident lie (a source publishing `null` because it
// lost its data would read 0 rpm). Header-only and free of SensESP, so the
// rules are unit-tested on the host (tests/) against ArduinoJson itself.

#include <ArduinoJson.h>
#include <math.h>

namespace skgauges {

// A numeric value: true and *out set, or false for null, a string, a
// boolean, an object or a missing value.
inline bool sk_number(JsonVariantConst value, float* out) {
  if (!value.is<float>()) {
    return false;
  }
  *out = value.as<float>();
  return true;
}

// navigation.attitude, an object {roll, pitch, yaw} in radians; any axis may
// be missing (NMEA 2000 PGN 127257 leaves out what the sender does not
// measure). True when roll is a number; *pitch is NAN if it is not. Yaw is
// not used.
inline bool sk_attitude(JsonVariantConst value, float* roll, float* pitch) {
  if (!sk_number(value["roll"], roll)) {
    return false;
  }
  if (!sk_number(value["pitch"], pitch)) {
    *pitch = NAN;
  }
  return true;
}

}  // namespace skgauges
