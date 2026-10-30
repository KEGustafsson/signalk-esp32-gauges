#include "gauge_settings.h"

#include <SPIFFS.h>
#include <esp_log.h>
#include <math.h>

#include "sensesp/ui/config_item.h"
#include "units.h"

namespace skgauges {

namespace {

constexpr const char* kTag = "gauges";
constexpr int kMinTimeoutS = 1;
constexpr int kMaxTimeoutS = 600;

const char* const kPageIds[kMaxPages] = {
    "page1", "page2", "page3", "page4",  "page5",  "page6",
    "page7", "page8", "page9", "page10", "page11", "page12"};

// The web UI's names for a page's style.
const char* style_name(GaugeKind kind) {
  switch (kind) {
    case GaugeKind::kCompass:
      return "Compass";
    case GaugeKind::kAttitude:
      return "Sea horizon";
    case GaugeKind::kRadial:
    default:
      return "Dial";
  }
}
bool parse_style(const String& name, GaugeKind* kind) {
  for (GaugeKind k :
       {GaugeKind::kRadial, GaugeKind::kCompass, GaugeKind::kAttitude}) {
    if (name == style_name(k)) {
      *kind = k;
      return true;
    }
  }
  return false;
}

// The definition a page of `kind` starts from: its factory page if that is
// the same style, else the style's template.
const GaugeDef& base_for(const GaugeDef* factory, GaugeKind kind) {
  return factory != nullptr && factory->kind == kind ? *factory
                                                     : style_template(kind);
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
      timeout_s((int)(kStaleAfterMs / 1000)),
      factory_(default_page(side, slot)) {
  const GaugeDef& def =
      factory_ != nullptr ? *factory_ : style_template(GaugeKind::kRadial);
  kind = def.kind;
  title = factory_ != nullptr ? def.title : "";
  sk_path = def.sk_path;
  unit_id = def.unit_id;
  min = def.min;
  max = def.max;
  decimals = def.decimals;
}

bool GaugeSettings::to_json(JsonObject& root) {
  root["style"] = style_name(kind);
  root["title"] = title;
  root["sk_path"] = sk_path;
  root["unit"] = unit_id;
  root["manual_scale"] = manual_scale;
  root["min"] = min;
  root["max"] = max;
  root["show_zones"] = show_zones;
  root["decimals"] = decimals;
  root["timeout_s"] = timeout_s;
  return true;
}

// Validates everything first and changes nothing on a bad value, so the web
// UI reports the error. Keys a save lacks keep their current values.
bool GaugeSettings::from_json(const JsonObject& config) {
  GaugeKind new_kind = kind;
  String new_title = title;
  String new_path = sk_path;
  String new_unit = unit_id;
  bool new_manual = manual_scale;
  float new_min = min;
  float new_max = max;
  int new_decimals = decimals;
  int new_timeout = timeout_s;

  if (config["style"].is<String>() &&
      !parse_style(config["style"].as<String>(), &new_kind)) {
    return false;
  }
  if (config["title"].is<String>()) {
    new_title = config["title"].as<String>();
    new_title.trim();
  }
  if (config["sk_path"].is<String>()) {
    new_path = config["sk_path"].as<String>();
    new_path.trim();
  }
  if (config["unit"].is<String>()) {
    new_unit = config["unit"].as<String>();
    if (find_display_unit(new_unit.c_str()) == nullptr) return false;
  }
  if (config["manual_scale"].is<bool>()) {
    new_manual = config["manual_scale"].as<bool>();
  }
  if (config["min"].is<float>()) new_min = config["min"].as<float>();
  if (config["max"].is<float>()) new_max = config["max"].as<float>();
  if (config["decimals"].is<int>()) new_decimals = config["decimals"].as<int>();

  // A style changed in the web UI starts from that style's defaults: a
  // heading path left on a sea horizon would never show anything. Only what
  // this save left as it was is reset, so a path typed in along with the new
  // style is kept. Not while loading at boot, where the saved style differs
  // from the factory one without anything having changed.
  if (loaded_ && new_kind != kind) {
    const GaugeDef& base = base_for(factory_, new_kind);
    if (new_title == title) new_title = &base == factory_ ? base.title : "";
    if (new_path == sk_path) new_path = base.sk_path;
    if (new_unit == unit_id) new_unit = base.unit_id;
    if (new_manual == manual_scale && new_min == min && new_max == max) {
      new_manual = false;
      new_min = base.min;
      new_max = base.max;
    }
    if (new_decimals == decimals) new_decimals = base.decimals;
  }
  if (new_path.isEmpty()) {
    // A compass or sea horizon falls back to its usual path. A dial left
    // without one is not subscribed (an empty path would subscribe to the
    // server's root) and shows dashes.
    new_path = base_for(factory_, new_kind).sk_path;
  }
  if (new_manual &&
      !(isfinite(new_min) && isfinite(new_max) && new_min < new_max)) {
    return false;
  }
  if (new_decimals < 0 || new_decimals > 3) return false;
  if (config["timeout_s"].is<int>()) {
    new_timeout = config["timeout_s"].as<int>();
    if (new_timeout < kMinTimeoutS || new_timeout > kMaxTimeoutS) {
      return false;
    }
  }

  if (config["show_zones"].is<bool>()) {
    show_zones = config["show_zones"].as<bool>();
  }
  kind = new_kind;
  title = new_title;
  sk_path = new_path;
  unit_id = new_unit;
  manual_scale = new_manual;
  min = new_min;
  max = new_max;
  decimals = new_decimals;
  timeout_s = new_timeout;
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

void GaugeConfig::take_snapshot(size_t index, const GaugeSettings& s) {
  Snapshot& snap = snapshot_[index];
  snap.base = base_for(s.factory(), s.kind);
  snap.base.id = kPageIds[index];
  snap.title = s.title;
  snap.sk_path = s.sk_path;
  snap.unit_id = s.unit_id;
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
      description += String(style_name(factory->kind)) + " \"" +
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

const String ConfigSchema(const skgauges::GaugeSettings&) {
  String units;
  for (size_t i = 0; i < skgauges::kDisplayUnitCount; i++) {
    if (i > 0) units += ",";
    units += String("\"") + skgauges::kDisplayUnits[i].id + "\"";
  }
  // A select is an array schema in SensESP's web UI; its value is a string.
  String schema = R"J({"type":"object","properties":{)J";
  schema += R"J("style":{"title":"Style","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":["Dial","Compass","Sea horizon"]},"description":"Dial: rpm, temperature, speed, any number. Compass: a heading. Sea horizon: navigation.attitude (heel and trim)."},)J";
  schema += R"J("title":{"title":"Title","type":"string","description":"Text on the dial face; empty for the style's default."},)J";
  schema += R"J("sk_path":{"title":"Signal K path","type":"string","description":"Empty: a compass or sea horizon reads its usual path; a dial shows dashes."},)J";
  schema += R"J("unit":{"title":"Display unit (dial)","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":[)J" +
            units + R"J(]},"description":"Converted from the path's Signal K (SI) unit; SI shows the value as sent."},)J";
  schema += R"J("manual_scale":{"title":"Set the scale by hand (dial)","type":"boolean","description":"Off: the server's displayScale for the path, else the default range."},)J";
  schema += R"J("min":{"title":"Scale minimum (dial)","type":"number"},)J";
  schema += R"J("max":{"title":"Scale maximum (dial)","type":"number"},)J";
  schema += R"J("show_zones":{"title":"Show Signal K zones (dial)","type":"boolean","description":"The path's meta.zones from the server: nominal, alert, warn, alarm, emergency."},)J";
  schema += R"J("decimals":{"title":"Decimals","type":"integer","minimum":0,"maximum":3},)J";
  schema += R"J("timeout_s":{"title":"Data timeout [s]","type":"integer","minimum":1,"maximum":600,"description":"A value older than this shows dashes."})J";
  schema += "}}";
  return schema;
}

}  // namespace sensesp
