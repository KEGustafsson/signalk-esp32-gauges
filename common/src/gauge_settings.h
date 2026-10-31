#pragma once

// The device's pages, set up the way SKip sets up its gauge widgets, in the
// SensESP web UI (Configuration): the "Pages" card sets how many pages are
// swiped through (1..12), and each of those has a card, "Page 1" ..
// "Page n" (page_rules.h: what it holds and how a save of it is treated),
// with one-shot actions to reset it to its factory setting or swap it with
// another page. Saved at /Pages/count and /Pages/<n>; a change takes effect
// after a restart (the subscriptions and the page list are set up at boot).
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
#include "page_rules.h"
#include "sensesp/signalk/signalk_metadata_listener.h"
#include "sensesp/system/saveable.h"

namespace skgauges {

class GaugeSettings : public sensesp::FileSystemSaveable {
 public:
  // Page `slot` (0-based) with the factory settings of a `side` device.
  GaugeSettings(size_t slot, Side side);

  bool to_json(JsonObject& root) override;
  // Parses a save (the web UI's, or the boot load) and applies the rules in
  // page_rules.h; also carries out the card's one-shot actions (reset to
  // factory, swap with another page). Changes nothing on a bad value.
  bool from_json(const JsonObject& config) override;
  // Writes the card to a staging file and checks it before it replaces the
  // card's file: unlike FileSystemSaveable::save(), which removes the old
  // file first and does not check the write, a failed write keeps the card's
  // last good setting. False if the card could not be written.
  bool save() override;

  // The factory page, or nullptr for a page past the factory ones.
  const GaugeDef* factory() const { return factory_; }
  size_t slot() const { return slot_; }
  // Call after the boot load: from then on a save is the web UI's, so a
  // style change resets the fields it left untouched (page_rules.h).
  void mark_loaded() { loaded_ = true; }

  PageFields fields;

 private:
  size_t slot_;
  const GaugeDef* factory_;
  bool loaded_ = false;
  // save() in two steps, so a swap can write both cards before it replaces
  // either: stage() writes and checks the staging file, commit() puts it in
  // place of the card's file.
  String staging_path() const;
  bool stage();
  bool commit();
  void discard_staged();
  // The revision a form loaded now carries (page_rules.h, PageAction): from
  // a random start each boot, so a form loaded before a restart is out of
  // date too, and moved on by a reset, a swap (both cards) or a save the
  // rules adjusted.
  uint32_t rev_;
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

  // The card of page `index` (0-based) if it is swiped through, else
  // nullptr. Only the web UI's handlers use the cards after boot.
  GaugeSettings* card(size_t index) const {
    return index < pages_.size() ? settings_[index].get() : nullptr;
  }

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
