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

size_t gauge_count() {
  return kGaugeCount < kMaxGauges ? kGaugeCount : kMaxGauges;
}

bool same_meta(const GaugeMeta& a, const GaugeMeta& b) {
  if (a.has_scale != b.has_scale || a.scale_lower != b.scale_lower ||
      a.scale_upper != b.scale_upper || a.zones.size() != b.zones.size()) {
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

GaugeSettings::GaugeSettings(size_t index)
    : sensesp::FileSystemSaveable(String("/Gauges/") + kGauges[index].id),
      shown(kGauges[index].shown_by_default),
      title(kGauges[index].title),
      sk_path(kGauges[index].sk_path),
      unit_id(kGauges[index].unit_id),
      min(kGauges[index].min),
      max(kGauges[index].max),
      decimals(kGauges[index].decimals),
      timeout_s((int)(kStaleAfterMs / 1000)),
      index_(index) {}

bool GaugeSettings::to_json(JsonObject& root) {
  root["shown"] = shown;
  root["title"] = title;
  root["sk_path"] = sk_path;
  root["decimals"] = decimals;
  root["timeout_s"] = timeout_s;
  if (base().kind == GaugeKind::kRadial) {
    root["unit"] = unit_id;
    root["manual_scale"] = manual_scale;
    root["min"] = min;
    root["max"] = max;
    root["show_zones"] = show_zones;
  }
  return true;
}

// Validates everything first and changes nothing on a bad value, so the web
// UI reports the error. Keys an older save lacks keep their defaults (the
// first version of this card held only sk_path).
bool GaugeSettings::from_json(const JsonObject& config) {
  String new_title = title;
  String new_path = sk_path;
  String new_unit = unit_id;
  bool new_manual = manual_scale;
  float new_min = min;
  float new_max = max;
  int new_decimals = decimals;
  int new_timeout = timeout_s;

  if (config["title"].is<String>()) {
    new_title = config["title"].as<String>();
    new_title.trim();
    if (new_title.isEmpty()) new_title = base().title;
  }
  if (config["sk_path"].is<String>()) {
    new_path = config["sk_path"].as<String>();
    new_path.trim();
    if (new_path.isEmpty()) {  // Would subscribe to the server's root
      return false;
    }
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
  if (new_manual &&
      !(isfinite(new_min) && isfinite(new_max) && new_min < new_max)) {
    return false;
  }
  if (config["decimals"].is<int>()) {
    new_decimals = config["decimals"].as<int>();
    if (new_decimals < 0 || new_decimals > 3) return false;
  }
  if (config["timeout_s"].is<int>()) {
    new_timeout = config["timeout_s"].as<int>();
    if (new_timeout < kMinTimeoutS || new_timeout > kMaxTimeoutS) {
      return false;
    }
  }

  if (config["shown"].is<bool>()) shown = config["shown"].as<bool>();
  if (config["show_zones"].is<bool>()) {
    show_zones = config["show_zones"].as<bool>();
  }
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

GaugeConfig& gauge_config() {
  static GaugeConfig instance;
  return instance;
}

void GaugeConfig::begin() {
  // SensESP mounts the same filesystem later; mounting again is a no-op.
  if (!SPIFFS.begin(true)) {
    ESP_LOGE(kTag, "Filesystem not mounted; gauges use their defaults");
  }
  for (size_t i = 0; i < gauge_count(); i++) {
    settings_[i] = std::make_shared<GaugeSettings>(i);
    settings_[i]->load();
    const GaugeSettings& s = *settings_[i];
    Snapshot& snap = snapshot_[i];
    snap.shown = s.shown;
    snap.title = s.title;
    snap.sk_path = s.sk_path;
    snap.unit_id = s.unit_id;
    snap.options.title = snap.title.c_str();
    snap.options.sk_path = snap.sk_path.c_str();
    snap.options.unit_id = snap.unit_id.c_str();
    snap.options.manual_scale = s.manual_scale;
    snap.options.min = s.min;
    snap.options.max = s.max;
    snap.options.decimals = s.decimals;
    snap.options.show_zones = s.show_zones;
    stale_ms_[i] = (uint32_t)s.timeout_s * 1000;
    resolve(i);
  }
}

void GaugeConfig::add_config_items() {
  for (size_t i = 0; i < gauge_count(); i++) {
    const GaugeDef& base = kGauges[i];
    String description = String("Like a SKip gauge. Default path: ") +
                         base.sk_path +
                         ". Zones come from the Signal K server's metadata "
                         "for the path (meta.zones).";
    sensesp::ConfigItem(settings_[i])
        ->set_title(String(base.title) + " (" + base.id + ")")
        ->set_description(description)
        ->set_requires_restart(true)
        ->set_sort_order(100 + (int)i);
  }
}

std::vector<size_t> GaugeConfig::pages(Side unit_side) const {
  const Side side = effective_side(unit_side);
  std::vector<size_t> list;
  for (size_t i = 0; i < gauge_count(); i++) {
    if (shown_on(kGauges[i], side) && snapshot_[i].shown) list.push_back(i);
  }
  if (list.empty()) {  // Every page switched off: show them rather than none
    for (size_t i = 0; i < gauge_count(); i++) {
      if (shown_on(kGauges[i], side)) list.push_back(i);
    }
  }
  return list;
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

  const DisplayUnit* unit = find_display_unit(defs_[index].unit_id);
  if (!view.units.isEmpty() && unit != nullptr && view.units != unit->si) {
    ESP_LOGW(kTag, "%s: the server gives %s in %s, the gauge converts from %s",
             kGauges[index].id, sk_path(index), view.units.c_str(), unit->si);
  }

  if (same_meta(meta, meta_[index])) {
    return;
  }
  meta_[index] = meta;
  resolve(index);
  generation_[index]++;
  ESP_LOGI(kTag, "%s: metadata applied (%u zones drawn, scale %.6g..%.6g)",
           kGauges[index].id, (unsigned)defs_[index].zone_count,
           defs_[index].min, defs_[index].max);
}

void GaugeConfig::resolve(size_t index) {
  defs_[index] =
      resolve_gauge(kGauges[index], snapshot_[index].options, meta_[index]);
}

}  // namespace skgauges

namespace sensesp {

const String ConfigSchema(const skgauges::GaugeSettings& settings) {
  using skgauges::GaugeKind;
  String units;
  for (size_t i = 0; i < skgauges::kDisplayUnitCount; i++) {
    if (i > 0) units += ",";
    units += String("\"") + skgauges::kDisplayUnits[i].id + "\"";
  }
  String schema = R"({"type":"object","properties":{)";
  schema += R"("shown":{"title":"Show this page","type":"boolean"},)";
  schema += R"("title":{"title":"Label","type":"string","description":"Text on the dial face."},)";
  schema += R"("sk_path":{"title":"Signal K path","type":"string"},)";
  if (settings.base().kind == GaugeKind::kRadial) {
    schema += R"("unit":{"title":"Display unit","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":[)" +
              units + R"(]},"description":"Converted from the path's Signal K (SI) unit."},)";
    schema += R"("manual_scale":{"title":"Set the scale by hand","type":"boolean","description":"Off: the server's displayScale for the path, else the default range."},)";
    schema += R"("min":{"title":"Scale minimum","type":"number"},)";
    schema += R"("max":{"title":"Scale maximum","type":"number"},)";
    schema += R"("show_zones":{"title":"Show Signal K zones","type":"boolean","description":"The path's meta.zones from the server: nominal, alert, warn, alarm, emergency."},)";
  }
  schema += R"("decimals":{"title":"Decimals","type":"integer","minimum":0,"maximum":3},)";
  schema += R"("timeout_s":{"title":"Data timeout [s]","type":"integer","minimum":1,"maximum":600,"description":"A value older than this shows dashes."})";
  schema += "}}";
  return schema;
}

}  // namespace sensesp
