#pragma once

// Type-strict Signal K listeners for the gauges.
//
// SensESP's SKValueListener<T>::parse_value() is `emit(json["value"].as<T>())`
// and ArduinoJson's as<T>() coerces: null, a string or an object all become
// 0.0f. For a gauge that is a lie -- a source publishing `null` ("no valid
// data", e.g. an engine gateway that lost its NMEA 2000 source) would park
// the needle on a confident 0 rpm. These listeners DROP any delta whose value
// is not a JSON number, so the gauge goes stale and shows dashes instead.
// (Same approach as KEGustafsson/drive-remote-controller's
// include/strict_sk_listeners.h.)
//
// Both expose the SK path in the SensESP web UI. SKListener keeps its
// to_json()/from_json() private, which ConfigItem cannot call, and its
// ConfigSchema() only describes listen_delay; SkPathConfig re-implements the
// two as public overrides and the ConfigSchema() overloads at the bottom
// describe `sk_path`, so the path is editable per unit.

#include <ArduinoJson.h>

#include <functional>

#include "sensesp/signalk/signalk_listener.h"
#include "sensesp/signalk/signalk_value_listener.h"

namespace skgauges {

// Public (de)serialisation of the listener's SK path for ConfigItem.
// `Base` must derive from sensesp::SKListener, whose sk_path is protected.
template <typename Base>
class SkPathConfig : public Base {
 public:
  using Base::Base;

  bool to_json(JsonObject& root) override {
    root["sk_path"] = this->sk_path;
    return true;
  }

  bool from_json(const JsonObject& config) override {
    if (!config["sk_path"].is<String>()) {
      return false;
    }
    this->sk_path = config["sk_path"].as<String>();
    return true;
  }
};

// Emits only for a real JSON number.
class StrictFloatListener
    : public SkPathConfig<sensesp::SKValueListener<float>> {
 public:
  using SkPathConfig<sensesp::SKValueListener<float>>::SkPathConfig;

  void parse_value(const JsonObject& json) override {
    auto value = json["value"];
    if (!value.is<float>()) {
      return;
    }
    this->emit(value.as<float>());
  }
};

// navigation.attitude is an object {roll, pitch, yaw} in radians. Calls the
// callback with roll and pitch when both are numbers; yaw is not used.
class AttitudeListener : public SkPathConfig<sensesp::SKListener> {
 public:
  using Callback = std::function<void(float roll_rad, float pitch_rad)>;

  AttitudeListener(const String& sk_path, int listen_delay,
                   const String& config_path, Callback callback)
      : SkPathConfig<sensesp::SKListener>(sk_path, listen_delay, config_path),
        callback_(std::move(callback)) {}

  void parse_value(const JsonObject& json) override {
    JsonVariantConst value = json["value"];
    if (!value["roll"].is<float>() || !value["pitch"].is<float>()) {
      return;
    }
    if (callback_) {
      callback_(value["roll"].as<float>(), value["pitch"].as<float>());
    }
  }

 private:
  Callback callback_;
};

}  // namespace skgauges

namespace sensesp {

inline const String SkPathConfigSchema() {
  return R"({"type":"object","properties":{"sk_path":{"title":"Signal K path","type":"string","description":"Signal K path this gauge listens to. Restart the device after changing it."}}})";
}

inline const String ConfigSchema(const skgauges::StrictFloatListener&) {
  return SkPathConfigSchema();
}

inline const String ConfigSchema(const skgauges::AttitudeListener&) {
  return SkPathConfigSchema();
}

}  // namespace sensesp
