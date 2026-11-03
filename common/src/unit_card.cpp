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
}  // namespace

UnitCard::UnitCard(const char* board)
    : sensesp::FileSystemSaveable("/Unit"), board_(board) {}

bool UnitCard::to_json(JsonObject& root) {
  const std::string hostname =
      sensesp::SensESPBaseApp::get_hostname().c_str();
  root["side"] = side_label(side_from_hostname(hostname));
  return true;
}

bool UnitCard::from_json(const JsonObject& config) {
  if (!config["side"].is<const char*>()) {
    return true;  // Nothing to change
  }
  Side side;
  if (!parse_side_label(config["side"].as<const char*>(), &side)) {
    return false;
  }
  const std::string current =
      sensesp::SensESPBaseApp::get_hostname().c_str();
  if (side_from_hostname(current) == side) {
    return true;  // Already that side (a name of the user's own is kept)
  }
  const std::string hostname = unit_hostname(board_, side);
  // SensESP's own hostname setting: saved at once, used from the restart.
  sensesp::SensESPBaseApp::get()->get_hostname_observable()->set(
      hostname.c_str());
  ESP_LOGI(kTag, "Side set to %s: hostname %s from the next restart",
           side_label(side), hostname.c_str());
  return true;
}

void add_unit_card(const char* board) {
  static std::shared_ptr<UnitCard> card = std::make_shared<UnitCard>(board);
  sensesp::ConfigItem(card)
      ->set_title("Unit")
      ->set_description(
          "Which side this unit is: its factory pages show that engine, "
          "and its name and network name follow it (gauge-...-stbd or "
          "gauge-...-port). Saving the other side sets the hostname; it "
          "takes effect when the unit restarts.")
      ->set_requires_restart(true)
      ->set_sort_order(98);
}

}  // namespace skgauges

namespace sensesp {

const String ConfigSchema(const skgauges::UnitCard&) {
  return R"J({"type":"object","properties":{"side":{"title":"Side","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":["Starboard","Port"]},"description":"Starboard or Port."}}})J";
}

}  // namespace sensesp
