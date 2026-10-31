#include "page_rules.h"

#include <math.h>
#include <string.h>

#include "gauge_resolve.h"
#include "units.h"

namespace skgauges {

PageFields factory_fields(const GaugeDef* factory) {
  const GaugeDef& def =
      factory != nullptr ? *factory : style_template(GaugeKind::kRadial);
  PageFields f;
  f.kind = def.kind;
  f.title = factory != nullptr ? def.title : "";
  f.sk_path = def.sk_path;
  f.unit_id = def.unit_id;
  f.min = def.min;
  f.max = def.max;
  f.decimals = def.decimals;
  return f;
}

const char* page_style_name(GaugeKind kind) {
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

bool parse_page_style(const char* name, GaugeKind* kind) {
  if (name == nullptr) {
    return false;
  }
  for (GaugeKind k :
       {GaugeKind::kRadial, GaugeKind::kCompass, GaugeKind::kAttitude}) {
    if (strcmp(name, page_style_name(k)) == 0) {
      *kind = k;
      return true;
    }
  }
  return false;
}

PageCheck finish_page_save(const PageFields& current, PageFields* next,
                           bool from_web) {
  if (from_web && next->kind != current.kind) {
    const GaugeDef& base = style_template(next->kind);
    if (next->title == current.title) next->title = "";  // Style's default
    if (next->sk_path == current.sk_path) next->sk_path = base.sk_path;
    if (next->unit_id == current.unit_id) next->unit_id = base.unit_id;
    if (next->manual_scale == current.manual_scale &&
        next->min == current.min && next->max == current.max) {
      next->manual_scale = false;
      next->min = base.min;
      next->max = base.max;
    }
    if (next->decimals == current.decimals) next->decimals = base.decimals;
  } else if (from_web && next->unit_id != current.unit_id &&
             next->sk_path == current.sk_path && !current.sk_path.empty() &&
             next->min == current.min && next->max == current.max) {
    convert_range(current.unit_id.c_str(), next->unit_id.c_str(), &next->min,
                  &next->max);
  }
  if (next->sk_path.empty()) {
    next->sk_path = style_template(next->kind).sk_path;
  }

  if (find_display_unit(next->unit_id.c_str()) == nullptr) {
    return PageCheck::kBadUnit;
  }
  if (!(isfinite(next->min) && isfinite(next->max) && next->min < next->max)) {
    return PageCheck::kBadRange;
  }
  if (next->decimals < 0 || next->decimals > 3) {
    return PageCheck::kBadDecimals;
  }
  if (next->timeout_s < kMinTimeoutS || next->timeout_s > kMaxTimeoutS) {
    return PageCheck::kBadTimeout;
  }
  return PageCheck::kOk;
}

std::string page_schema(GaugeKind kind, size_t page_count) {
  std::string units;
  for (size_t i = 0; i < kDisplayUnitCount; i++) {
    if (i > 0) units += ",";
    units += std::string("\"") + kDisplayUnits[i].id + "\"";
  }
  // A select is an array schema in SensESP's web UI; its value is a string.
  std::string s = R"J({"type":"object","properties":{)J";
  s += R"J("style":{"title":"Style","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":["Dial","Compass","Sea horizon"]},"description":"Dial: rpm, temperature, speed, any number. Compass: a heading. Sea horizon: navigation.attitude (heel and trim). Save and reload the page to see the new style's settings."},)J";
  s += R"J("title":{"title":"Title","type":"string","description":"Text on the dial face; empty for the style's default."},)J";
  s += R"J("sk_path":{"title":"Signal K path","type":"string","description":"Empty: a compass or sea horizon reads its usual path; a dial shows dashes."},)J";
  if (kind == GaugeKind::kRadial) {
    s += R"J("unit":{"title":"Display unit","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":[)J" +
         units +
         R"J(]},"description":"Converted from the path's Signal K (SI) unit; SI shows the value as sent."},)J";
    s += R"J("manual_scale":{"title":"Set the scale by hand","type":"boolean","description":"Off: the server's displayScale for the path when it sends one, else the scale below."},)J";
    s += R"J("min":{"title":"Scale minimum","type":"number"},)J";
    s += R"J("max":{"title":"Scale maximum","type":"number"},)J";
    s += R"J("show_zones":{"title":"Show Signal K zones","type":"boolean","description":"The path's meta.zones from the server: nominal, alert, warn, alarm, emergency."},)J";
  }
  s += R"J("decimals":{"title":"Decimals","type":"integer","minimum":0,"maximum":3},)J";
  s += R"J("timeout_s":{"title":"Data timeout [s]","type":"integer","minimum":1,"maximum":600,"description":"A value older than this shows dashes."},)J";
  s += R"J("reset":{"title":"Reset to factory settings","type":"boolean","description":"Tick and save: this page goes back to its factory setting (other changes in the same save are ignored)."},)J";
  s += R"J("swap_with":{"title":"Swap with page","type":"integer","minimum":0,"maximum":)J" +
       std::to_string(page_count) +
       R"J(,"description":"A page number and save: the two pages change places (with this save's changes). 0: no swap."})J";
  s += "}}";
  return s;
}

}  // namespace skgauges
