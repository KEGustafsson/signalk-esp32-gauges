#pragma once

// Each gauge page is set up the way SKip sets up a gauge widget, in the
// SensESP web UI (Configuration -> one card per page, e.g.
// "PRT Engine (port_rpm)"): shown or not, label, Signal K path, display
// unit, scale (from the server's meta.displayScale, or by hand), decimals,
// whether to draw the server's zones, and the data timeout. Saved at
// /Gauges/<id>; a change takes effect after a restart (the subscription and
// the page list are set up at boot).
//
// GaugeConfig then holds, for every page, a snapshot of those settings taken
// at boot, the Signal K metadata received for its path (zones, displayScale)
// and the definition the page is drawn from (gauge_resolve.h). Everything
// but the web UI's own load/save runs on the loop task.

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <vector>

#include "gauge_defs.h"
#include "gauge_resolve.h"
#include "sensesp/signalk/signalk_metadata_listener.h"
#include "sensesp/system/saveable.h"

namespace skgauges {

class GaugeSettings : public sensesp::FileSystemSaveable {
 public:
  explicit GaugeSettings(size_t index);

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;

  const GaugeDef& base() const { return kGauges[index_]; }

  bool shown;
  String title;
  String sk_path;
  String unit_id;
  bool manual_scale = false;
  float min;
  float max;
  int decimals;
  bool show_zones = true;
  int timeout_s;

 private:
  size_t index_;
};

class GaugeConfig {
 public:
  // Loads every page's settings. Mounts the filesystem first if SensESP has
  // not yet (the first page is drawn before SensESP starts).
  void begin();
  // Adds the web UI cards; call once SensESP's app exists.
  void add_config_items();

  // The pages this unit shows, in swipe order: those switched on and meant
  // for this side -- or, if that leaves none, every page for this side.
  std::vector<size_t> pages(Side unit_side) const;

  const GaugeDef& def(size_t index) const { return defs_[index]; }
  // Changes whenever def(index) does (metadata arrived), so a page drawn
  // from an older one is rebuilt.
  uint32_t generation(size_t index) const { return generation_[index]; }
  uint32_t stale_ms(size_t index) const { return stale_ms_[index]; }
  // The Signal K path the page listens to.
  const char* sk_path(size_t index) const { return def(index).sk_path; }

  // Metadata from the server for page `index`'s path.
  void set_meta(size_t index, const sensesp::SKMetaView& meta);

 private:
  struct Snapshot {
    bool shown = true;
    String title;
    String sk_path;
    String unit_id;
    GaugeOptions options;
  };

  void resolve(size_t index);

  std::shared_ptr<GaugeSettings> settings_[kMaxGauges];
  Snapshot snapshot_[kMaxGauges];
  GaugeMeta meta_[kMaxGauges];
  GaugeDef defs_[kMaxGauges] = {};
  uint32_t generation_[kMaxGauges] = {};
  uint32_t stale_ms_[kMaxGauges] = {};
};

GaugeConfig& gauge_config();

}  // namespace skgauges

namespace sensesp {
const String ConfigSchema(const skgauges::GaugeSettings& settings);
}  // namespace sensesp
