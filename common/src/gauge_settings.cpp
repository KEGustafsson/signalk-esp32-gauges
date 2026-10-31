#include "gauge_settings.h"

#include <SPIFFS.h>
#include <esp_random.h>
#include <esp_log.h>
#include <math.h>

#include "sensesp/ui/config_item.h"
#include "units.h"

namespace skgauges {

namespace {

constexpr const char* kTag = "gauges";
const char* const kPageIds[kMaxPages] = {
    "page1", "page2", "page3", "page4",  "page5",  "page6",
    "page7", "page8", "page9", "page10", "page11", "page12"};

// A card's string: trimmed, or `fallback` when the save does not carry it.
std::string json_string(JsonVariantConst value, const std::string& fallback) {
  const char* text = value.as<const char*>();
  if (!value.is<const char*>() || text == nullptr) {
    return fallback;
  }
  std::string s = text;
  const size_t first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return "";
  }
  const size_t last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

const char* check_text(PageCheck check) {
  switch (check) {
    case PageCheck::kBadUnit:
      return "unknown display unit";
    case PageCheck::kBadRange:
      return "scale minimum must be below its maximum";
    case PageCheck::kBadDecimals:
      return "decimals must be 0..3";
    case PageCheck::kBadTimeout:
      return "data timeout must be 1..600 s";
    case PageCheck::kOk:
    default:
      return "ok";
  }
}

bool same_meta(const GaugeMeta& a, const GaugeMeta& b) {
  if (a.has_scale != b.has_scale || a.scale_lower != b.scale_lower ||
      a.scale_upper != b.scale_upper || a.units != b.units ||
      a.zones.size() != b.zones.size()) {
    return false;
  }
  for (size_t i = 0; i < a.zones.size(); i++) {
    const SiZone& x = a.zones[i];
    const SiZone& y = b.zones[i];
    if (x.has_lower != y.has_lower || x.lower != y.lower ||
        x.has_upper != y.has_upper || x.upper != y.upper ||
        x.color != y.color) {
      return false;
    }
  }
  return true;
}

}  // namespace

GaugeSettings::GaugeSettings(size_t slot, Side side)
    : sensesp::FileSystemSaveable(String("/Pages/") + (int)(slot + 1)),
      fields(factory_fields(default_page(side, slot))),
      slot_(slot),
      factory_(default_page(side, slot)),
      rev_(esp_random()) {}

bool GaugeSettings::to_json(JsonObject& root) {
  root["style"] = page_style_name(fields.kind);
  root["title"] = fields.title;
  root["sk_path"] = fields.sk_path;
  root["unit"] = fields.unit_id;
  root["manual_scale"] = fields.manual_scale;
  root["min"] = fields.min;
  root["max"] = fields.max;
  root["show_zones"] = fields.show_zones;
  root["decimals"] = fields.decimals;
  root["timeout_s"] = fields.timeout_s;
  root["reset"] = false;  // One-shot actions, never stored
  root["swap_with"] = 0;
  root["_rev"] = rev_;  // Hidden in the web UI, sent back with a save
  return true;
}

bool GaugeSettings::from_json(const JsonObject& config) {
  const unsigned page = (unsigned)(slot_ + 1);
  const bool form_current =
      config["_rev"].is<uint32_t>() && config["_rev"].as<uint32_t>() == rev_;
  const bool reset = config["reset"].is<bool>() && config["reset"].as<bool>();
  const int swap = config["swap_with"].is<int>() ? config["swap_with"].as<int>()
                                                 : 0;
  const PageAction action = page_save_action(
      loaded_, form_current, reset, swap, slot_, gauge_config().pages().size());
  if (action == PageAction::kStale) {
    ESP_LOGW(kTag,
             "Page %u not saved: the form is out of date; reload the page",
             page);
    return false;
  }
  if (action == PageAction::kBadSwap) {
    ESP_LOGW(kTag, "Page %u not saved: no page %d to swap with", page, swap);
    return false;
  }
  if (action == PageAction::kReset) {
    fields = factory_fields(factory_);
    rev_++;
    ESP_LOGI(kTag, "Page %u reset to its factory setting", page);
    return true;
  }

  PageFields next = fields;
  if (config["style"].is<const char*>() &&
      !parse_page_style(config["style"].as<const char*>(), &next.kind)) {
    ESP_LOGW(kTag, "Page %u not saved: unknown style", page);
    return false;
  }
  next.title = json_string(config["title"], next.title);
  next.sk_path = json_string(config["sk_path"], next.sk_path);
  next.unit_id = json_string(config["unit"], next.unit_id);
  if (config["manual_scale"].is<bool>()) {
    next.manual_scale = config["manual_scale"].as<bool>();
  }
  if (config["min"].is<float>()) next.min = config["min"].as<float>();
  if (config["max"].is<float>()) next.max = config["max"].as<float>();
  if (config["show_zones"].is<bool>()) {
    next.show_zones = config["show_zones"].as<bool>();
  }
  if (config["decimals"].is<int>()) next.decimals = config["decimals"].as<int>();
  if (config["timeout_s"].is<int>()) {
    next.timeout_s = config["timeout_s"].as<int>();
  }
  const PageFields submitted = next;
  const PageCheck check = finish_page_save(fields, &next, loaded_);
  if (check != PageCheck::kOk) {
    ESP_LOGW(kTag, "Page %u not saved: %s", page, check_text(check));
    return false;
  }

  if (action == PageAction::kSwap) {
    // This page (with this save's changes) goes to the other card and the
    // other card's page comes here. Only the web UI's handlers touch the
    // cards after boot.
    GaugeSettings* other = gauge_config().card((size_t)swap - 1);
    if (other == nullptr) {
      ESP_LOGW(kTag, "Page %u not saved: no page %d to swap with", page, swap);
      return false;
    }
    // Both cards are written here, the other first, and put back if either
    // write fails, so a full or failing filesystem never leaves one page on
    // both cards (and the other page lost) after a restart. (The web UI
    // writes this card once more after from_json(): the same contents.)
    const PageFields mine = fields;
    const PageFields theirs = other->fields;
    other->fields = next;
    if (!other->save()) {
      other->fields = theirs;
      ESP_LOGE(kTag, "Pages %u and %d not swapped: page %d could not be written",
               page, swap, swap);
      return false;
    }
    fields = theirs;
    if (!save()) {
      fields = mine;
      other->fields = theirs;
      if (!other->save()) {
        ESP_LOGE(kTag, "Page %d could not be restored; check both pages",
                 swap);
      }
      ESP_LOGE(kTag, "Pages %u and %d not swapped: page %u could not be written",
               page, swap, page);
      return false;
    }
    other->rev_++;  // Both open forms now show the other page
    rev_++;
    ESP_LOGI(kTag, "Pages %u and %d swapped", page, swap);
    return true;
  }
  fields = next;
  if (!same_fields(submitted, next)) {
    rev_++;  // The rules changed what the form shows: it must reload
  }
  return true;
}

PageCount::PageCount()
    : sensesp::FileSystemSaveable("/Pages/count"),
      count((int)factory_page_count()) {}

bool PageCount::to_json(JsonObject& root) {
  root["count"] = count;
  return true;
}

bool PageCount::from_json(const JsonObject& config) {
  if (!config["count"].is<int>()) {
    return true;  // Nothing to change
  }
  const int value = config["count"].as<int>();
  if (value < 1 || value > (int)kMaxPages) {
    return false;
  }
  count = value;
  return true;
}

GaugeConfig& gauge_config() {
  static GaugeConfig instance;
  return instance;
}

void GaugeConfig::begin(Side side) {
  side_ = side;
  // SensESP mounts the same filesystem later; mounting again is a no-op.
  if (!SPIFFS.begin(true)) {
    ESP_LOGE(kTag, "Filesystem not mounted; pages use their defaults");
  }
  page_count_ = std::make_shared<PageCount>();
  page_count_->load();
  // Only the pages swiped through are loaded: each load costs a SPIFFS
  // lookup at boot, before touch answers.
  pages_.clear();
  for (size_t i = 0; i < (size_t)page_count_->count && i < kMaxPages; i++) {
    settings_[i] = std::make_shared<GaugeSettings>(i, side);
    settings_[i]->load();
    settings_[i]->mark_loaded();
    take_snapshot(i, *settings_[i]);
    pages_.push_back(i);
  }
}

void GaugeConfig::take_snapshot(size_t index, const GaugeSettings& card) {
  const PageFields& s = card.fields;
  Snapshot& snap = snapshot_[index];
  // Everything comes from the card, nothing from the slot: a style's
  // template with the card's unit and range (ticks to suit the range).
  snap.unit_id = s.unit_id.c_str();
  snap.base = style_template(s.kind);
  snap.base.id = kPageIds[index];
  if (s.kind == GaugeKind::kRadial &&
      find_display_unit(s.unit_id.c_str()) != nullptr && s.min < s.max) {
    snap.base.unit_id = snap.unit_id.c_str();
    snap.base.min = s.min;
    snap.base.max = s.max;
    snap.base.major_step =
        nice_step(s.min, s.max, &snap.base.minor_per_major);
  }
  snap.title = s.title.c_str();
  snap.sk_path = s.sk_path.c_str();
  snap.options = GaugeOptions();
  snap.options.title = snap.title.c_str();
  snap.options.sk_path = snap.sk_path.c_str();
  snap.options.unit_id = snap.unit_id.c_str();
  snap.options.manual_scale = s.manual_scale;
  snap.options.min = s.min;
  snap.options.max = s.max;
  snap.options.decimals = s.decimals;
  snap.options.show_zones = s.show_zones;
  stale_ms_[index] = (uint32_t)s.timeout_s * 1000;
  resolve(index);
}

void GaugeConfig::add_config_items() {
  sensesp::ConfigItem(page_count_)
      ->set_title("Pages")
      ->set_description(
          "How many pages to swipe through: Page 1 up to this one, in order. "
          "Each page is set up in its own card below.")
      ->set_requires_restart(true)
      ->set_sort_order(99);
  for (size_t i = 0; i < pages_.size(); i++) {
    const GaugeDef* factory = settings_[i]->factory();
    String description =
        "Like a SKip gauge. Zones come from the Signal K server's metadata "
        "for the path (meta.zones). Factory setting: ";
    if (factory != nullptr) {
      description += String(page_style_name(factory->kind)) + " \"" +
                     factory->title + "\" on " + factory->sk_path + ".";
    } else {
      description += "a dial with no path.";
    }
    sensesp::ConfigItem(settings_[i])
        ->set_title(String("Page ") + (int)(i + 1))
        ->set_description(description)
        ->set_requires_restart(true)
        ->set_sort_order(100 + (int)i);
  }
}

void GaugeConfig::set_meta(size_t index, const sensesp::SKMetaView& view) {
  GaugeMeta meta;
  if (view.raw) {
    JsonVariantConst scale = (*view.raw)["value"]["displayScale"];
    if (scale["lower"].is<float>() && scale["upper"].is<float>()) {
      meta.has_scale = true;
      meta.scale_lower = scale["lower"].as<float>();
      meta.scale_upper = scale["upper"].as<float>();
    }
  }
  for (const sensesp::SKZone& z : view.zones) {
    SiZone zone;
    zone.has_lower = z.has_lower;
    zone.lower = z.lower;
    zone.has_upper = z.has_upper;
    zone.upper = z.upper;
    zone.color = zone_state_color(z.state.c_str());
    meta.zones.push_back(zone);
  }
  meta.units = view.units.c_str();

  // Zones and scale are in the path's unit; converted as if they were in
  // another, they would mark the wrong part of the dial. Drop them.
  const DisplayUnit* unit = find_display_unit(defs_[index].unit_id);
  if (!view.units.isEmpty() && unit != nullptr && !is_as_sent(*unit) &&
      view.units != unit->si) {
    ESP_LOGW(kTag,
             "%s: the server gives %s in %s, the gauge converts from %s; "
             "its zones and scale are not used",
             defs_[index].id, sk_path(index), view.units.c_str(), unit->si);
    meta = GaugeMeta();
  }

  if (same_meta(meta, meta_[index])) {
    return;
  }
  meta_[index] = meta;
  resolve(index);
  generation_[index]++;
  ESP_LOGI(kTag, "%s: metadata applied (%u zones drawn, scale %.6g..%.6g)",
           defs_[index].id, (unsigned)defs_[index].zone_count,
           defs_[index].min, defs_[index].max);
}

void GaugeConfig::resolve(size_t index) {
  defs_[index] = resolve_gauge(snapshot_[index].base, snapshot_[index].options,
                               meta_[index]);
}

}  // namespace skgauges

namespace sensesp {

const String ConfigSchema(const skgauges::PageCount&) {
  return R"J({"type":"object","properties":{"count":{"title":"Number of pages","type":"integer","minimum":1,"maximum":12,"description":"Pages swiped through, from Page 1."}}})J";
}

const String ConfigSchema(const skgauges::GaugeSettings& card) {
  // Built on every request, so the card shows the fields of the page's
  // style as last saved.
  return skgauges::page_schema(card.fields.kind,
                               skgauges::gauge_config().pages().size())
      .c_str();
}

}  // namespace sensesp
