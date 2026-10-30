#pragma once

// The device's pages, set up the way SKip sets up its gauge widgets, in the
// SensESP web UI (Configuration): the "Pages" card sets how many pages are
// swiped through (1..12), and each of those has a card, "Page 1" ..
// "Page n", with one of three styles -- a steel dial, a compass card or the
// sea horizon -- its title, Signal K path, display unit, scale (by hand or
// from the server's meta.displayScale), whether to draw the server's zones,
// decimals and data timeout. Saved at /Pages/count and /Pages/<n>; a change
// takes effect after a restart (the subscriptions and the page list are set
// up at boot).
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
  // Page `slot` (0-based) with the factory settings of a `side` device.
  GaugeSettings(size_t slot, Side side);

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;

  // The factory page, or nullptr for a page past the factory ones.
  const GaugeDef* factory() const { return factory_; }
  // Call after the boot load: from then on, a save that changes the style
  // resets the fields it left untouched to the new style's defaults.
  void mark_loaded() { loaded_ = true; }

  GaugeKind kind;
  String title;  // Empty: the style's default
  String sk_path;  // Empty: a dial that is not subscribed (dashes)
  String unit_id;
  bool manual_scale = false;
  float min;
  float max;
  int decimals;
  bool show_zones = true;
  int timeout_s;

 private:
  const GaugeDef* factory_;
  bool loaded_ = false;
};

// How many pages are swiped through: Page 1 .. Page `count`.
class PageCount : public sensesp::FileSystemSaveable {
 public:
  PageCount();

  bool to_json(JsonObject& root) override;
  bool from_json(const JsonObject& config) override;

  int count;
};

class GaugeConfig {
 public:
  // Loads every page's settings. Mounts the filesystem first if SensESP has
  // not yet (the first page is drawn before SensESP starts).
  void begin(Side side);
  // Adds the web UI cards; call once SensESP's app exists.
  void add_config_items();

  // The pages swiped through, in order: 0 .. count - 1 (at least one).
  const std::vector<size_t>& pages() const { return pages_; }

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
    String title;
    String sk_path;
    String unit_id;
    GaugeOptions options;
    GaugeDef base = {};
  };

  void take_snapshot(size_t index, const GaugeSettings& settings);
  void resolve(size_t index);

  Side side_ = Side::kStarboard;
  std::shared_ptr<PageCount> page_count_;
  std::shared_ptr<GaugeSettings> settings_[kMaxPages];
  Snapshot snapshot_[kMaxPages];
  GaugeMeta meta_[kMaxPages];
  GaugeDef defs_[kMaxPages] = {};
  uint32_t generation_[kMaxPages] = {};
  uint32_t stale_ms_[kMaxPages] = {};
  std::vector<size_t> pages_;
};

GaugeConfig& gauge_config();

}  // namespace skgauges

namespace sensesp {
const String ConfigSchema(const skgauges::PageCount& count);
const String ConfigSchema(const skgauges::GaugeSettings& settings);
}  // namespace sensesp
