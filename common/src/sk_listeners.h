#pragma once

// Type-strict Signal K listeners for the gauges.
//
// SensESP's SKValueListener<T>::parse_value() is `emit(json["value"].as<T>())`
// and ArduinoJson's as<T>() coerces: null, a string or an object all become
// 0.0f. For a gauge that is a lie -- a source publishing `null` ("no valid
// data", e.g. an engine gateway that lost its NMEA 2000 source) would park
// the needle on a confident 0 rpm. These listeners DROP any delta whose value
// is not a JSON number and report it, so the gauge shows dashes (within a
// few seconds, unless another source of the same path is still delivering
// numbers) instead of a number nobody vouches for.
// (Same approach as KEGustafsson/drive-remote-controller's
// include/strict_sk_listeners.h.)
//
// The path's Signal K metadata (zones, displayScale, units) comes through a
// separate sensesp::SKMetadataListener on the same path (sk_data.cpp):
// SensESP hands value deltas only to listeners whose wants_meta() is false,
// and metadata only to those where it is true. Its subscription merges the
// two into one entry, so one subscription per page still carries both. The
// path itself is set in the page's settings card (gauge_settings.h).

#include <ArduinoJson.h>
#include <math.h>

#include <functional>
#include <memory>

#include "sensesp/signalk/signalk_listener.h"
#include "sensesp/signalk/signalk_metadata_listener.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sk_parse.h"

namespace skgauges {

// The callback shared by the gauge listeners: a delta whose value is not
// usable. `Base` derives from SKListener. Never override wants_meta() here:
// a listener that wants metadata gets no values (see above).
template <typename Base>
class GaugeListener : public Base {
 public:
  using Base::Base;

  // Called for a delta whose value is null or not a number: the source is
  // saying it has no valid reading, so the gauge should stop showing one.
  void set_invalid_callback(std::function<void()> callback) {
    on_invalid_ = std::move(callback);
  }

 protected:
  void invalid() {
    if (on_invalid_) on_invalid_();
  }

 private:
  std::function<void()> on_invalid_;
};

// Emits only for a real JSON number.
class StrictFloatListener
    : public GaugeListener<sensesp::SKValueListener<float>> {
 public:
  using GaugeListener<sensesp::SKValueListener<float>>::GaugeListener;

  void parse_value(const JsonObject& json) override {
    float value;
    if (!sk_number(json["value"], &value)) {
      invalid();
      return;
    }
    this->emit(value);
  }
};

// navigation.attitude is an object {roll, pitch, yaw} in radians; any axis
// may be missing (NMEA 2000 PGN 127257 leaves out what the sender does not
// measure). Calls the callback when roll is a number, with pitch NAN if it is
// not; yaw is not used.
class AttitudeListener : public GaugeListener<sensesp::SKListener> {
 public:
  using Callback = std::function<void(float roll_rad, float pitch_rad)>;

  AttitudeListener(const String& sk_path, int listen_delay, Callback callback)
      : GaugeListener<sensesp::SKListener>(sk_path, listen_delay),
        callback_(std::move(callback)) {}

  void parse_value(const JsonObject& json) override {
    float roll;
    float pitch;
    if (!sk_attitude(json["value"], &roll, &pitch)) {
      invalid();
      return;
    }
    if (callback_) {
      callback_(roll, pitch);
    }
  }

 private:
  Callback callback_;
};

}  // namespace skgauges
