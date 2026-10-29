#pragma once

// Type-strict Signal K listeners for the gauges.
//
// SensESP's SKValueListener<T>::parse_value() is `emit(json["value"].as<T>())`
// and ArduinoJson's as<T>() coerces: null, a string or an object all become
// 0.0f. For a gauge that is a lie -- a source publishing `null` ("no valid
// data", e.g. an engine gateway that lost its NMEA 2000 source) would park
// the needle on a confident 0 rpm. These listeners DROP any delta whose value
// is not a JSON number and report it, so the gauge shows dashes (at once,
// unless another source of the same path is still delivering numbers)
// instead of a number nobody vouches for.
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

// Public (de)serialisation of the listener's SK path for ConfigItem, plus a
// callback for deltas whose value is not usable.
// `Base` must derive from sensesp::SKListener, whose sk_path is protected.
//
// The web UI saves a path from the HTTP server task while the event loop and
// the WebSocket task read it, so both directions hold SKListener's own
// (recursive) semaphore -- the lock SensESP takes around those reads.
template <typename Base>
class SkPathConfig : public Base {
 public:
  using Base::Base;

  bool to_json(JsonObject& root) override {
    sensesp::SKListener::take_semaphore();
    root["sk_path"] = this->sk_path;
    sensesp::SKListener::release_semaphore();
    return true;
  }

  bool from_json(const JsonObject& config) override {
    if (!config["sk_path"].is<String>()) {
      return false;
    }
    String path = config["sk_path"].as<String>();
    sensesp::SKListener::take_semaphore();
    this->sk_path = path;
    sensesp::SKListener::release_semaphore();
    return true;
  }

  // A copy of the current path, safe to call from any task.
  String sk_path_copy() {
    sensesp::SKListener::take_semaphore();
    String path = this->sk_path;
    sensesp::SKListener::release_semaphore();
    return path;
  }

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
    : public SkPathConfig<sensesp::SKValueListener<float>> {
 public:
  using SkPathConfig<sensesp::SKValueListener<float>>::SkPathConfig;

  void parse_value(const JsonObject& json) override {
    auto value = json["value"];
    if (!value.is<float>()) {
      invalid();
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
      invalid();
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
