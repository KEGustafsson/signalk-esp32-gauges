#include "unit_card.h"

#include <esp_log.h>

#include <memory>
#include <string>

#include "sensesp/ui/config_item.h"
#include "sensesp_base_app.h"
#include "unit_identity.h"

namespace skgauges {

namespace {
constexpr const char* kTag = "unit";

std::string current_hostname() {
  return sensesp::SensESPBaseApp::get_hostname().c_str();
}
}  // namespace

bool UnitSettings::to_json(JsonObject& root) {
  if (has_role) {
    root["role"] = side_label(role);
  }
  root["name"] = name.c_str();
  return true;
}

bool UnitSettings::from_json(const JsonObject& config) {
  Side parsed;
  if (config["role"].is<const char*>() &&
      parse_side_label(config["role"].as<const char*>(), &parsed)) {
    role = parsed;
    has_role = true;
  }
  if (config["name"].is<const char*>()) {
    const std::string value = config["name"].as<const char*>();
    name = valid_display_name(value) ? value : "";
  }
  return true;
}

UnitSettings& unit_settings() {
  static UnitSettings settings;
  return settings;
}

UnitCard::UnitCard(const char* board, const char* board_label, Side role)
    : sensesp::FileSystemSaveable("/Unit"),
      board_(board),
      board_label_(board_label),
      role_(role) {}

bool UnitCard::to_json(JsonObject& root) {
  const UnitSettings& s = unit_settings();
  root["role"] = side_label(role_);
  root["hostname"] = current_hostname().c_str();
  root["name"] = s.name.empty() ? unit_name(board_label_, role_).c_str()
                                : s.name.c_str();
  return true;
}

bool UnitCard::from_json(const JsonObject& config) {
  Side role = role_;
  if (config["role"].is<const char*>() &&
      !parse_side_label(config["role"].as<const char*>(), &role)) {
    return false;
  }
  std::string hostname;
  if (config["hostname"].is<const char*>()) {
    hostname = config["hostname"].as<const char*>();
    if (!hostname.empty() && !valid_hostname(hostname)) {
      ESP_LOGW(kTag, "Not saved: \"%s\" is not a hostname (a-z, 0-9, '-')",
               hostname.c_str());
      return false;
    }
  }
  std::string name;
  if (config["name"].is<const char*>()) {
    name = config["name"].as<const char*>();
    if (!name.empty() && !valid_display_name(name)) {
      ESP_LOGW(kTag, "Not saved: the display name must be 1-24 characters");
      return false;
    }
  }

  const std::string current = current_hostname();
  const std::string next =
      hostname_after_save(board_, current, role_, role, hostname);
  if (next != current) {
    // SensESP's own hostname setting: saved at once, used from the restart.
    sensesp::SensESPBaseApp::get()->get_hostname_observable()->set(
        next.c_str());
  }
  UnitSettings& s = unit_settings();
  // The old role's default name (sent back unchanged by the form) follows
  // the role; anything else is the user's own.
  if (name.empty() || name == unit_name(board_label_, role_)) {
    s.name.clear();
  } else {
    s.name = name;
  }
  s.role = role;
  s.has_role = true;
  role_ = role;
  ESP_LOGI(kTag, "Unit: %s, hostname %s, name %s (from the next restart)",
           side_label(role), next.c_str(),
           s.name.empty() ? unit_name(board_label_, role).c_str()
                          : s.name.c_str());
  return true;
}

bool UnitCard::save() { return unit_settings().save(); }

void add_unit_card(const char* board, const char* board_label, Side role) {
  static std::shared_ptr<UnitCard> card =
      std::make_shared<UnitCard>(board, board_label, role);
  sensesp::ConfigItem(card)
      ->set_title("Unit")
      ->set_description(
          "What this unit is. Role: a starboard or a port engine display, "
          "or a general one (no engine); it picks the pages a reset page "
          "goes back to and the unit's address in secrets.h. Hostname: its "
          "network name (it follows the role while it is a built-in name). "
          "Display name: shown on the gauge. Takes effect when the unit "
          "restarts.")
      ->set_requires_restart(true)
      ->set_sort_order(98);
}

}  // namespace skgauges

namespace sensesp {

const String ConfigSchema(const skgauges::UnitCard&) {
  return R"J({"type":"object","properties":{)J"
         R"J("role":{"title":"Role","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":["Starboard","Port","General"]},"description":"Starboard or Port: an engine display for that side. General: no engine (navigation, wind, battery)."},)J"
         R"J("hostname":{"title":"Hostname","type":"string","description":"The unit's network name (name.local), a-z, 0-9 and '-', up to 32. Leave it as it is to let it follow the role while it is a built-in name."},)J"
         R"J("name":{"title":"Display name","type":"string","description":"Shown on the gauge's info overlay, up to 24 characters."})J"
         R"J(}})J";
}

}  // namespace sensesp
