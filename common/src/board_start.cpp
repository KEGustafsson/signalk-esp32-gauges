#include "board_start.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_attr.h>
#include <esp_display_panel.hpp>
#include <esp_log.h>
#include <esp_system.h>


namespace skgauges {

namespace {

using esp_panel::board::Board;
using esp_panel::board::BoardConfig;
using esp_panel::drivers::BusI2C;
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ESP_REV_MIN_FULL >= 300
using esp_panel::drivers::BusDSI;
#endif

constexpr const char* kTag = "board";
constexpr const char* kPrefsNamespace = "skgauges";
constexpr const char* kTouchAddressKey = "touch_addr";
constexpr uint8_t kDefaultAddress = 0;  // The board file's address
constexpr uint32_t kRtcMagic = 0x5D14A5C3;

// Survive esp_restart() but not power loss; the magic tells real values
// from power-on garbage.
RTC_NOINIT_ATTR uint32_t rtc_magic;
RTC_NOINIT_ATTR uint32_t rtc_attempt;

uint8_t load_touch_address() {
  Preferences prefs;
  uint8_t address = kDefaultAddress;
  // Read-write: a read-only open of a namespace that does not exist yet
  // logs an error on every fresh unit.
  if (prefs.begin(kPrefsNamespace, false)) {
    if (prefs.isKey(kTouchAddressKey)) {
      address = prefs.getUChar(kTouchAddressKey, kDefaultAddress);
    }
    prefs.end();
  }
  return address;
}

void save_touch_address(uint8_t address) {
  Preferences prefs;
  if (prefs.begin(kPrefsNamespace, false)) {
    prefs.putUChar(kTouchAddressKey, address);
    prefs.end();
  }
}

}  // namespace

Board* start_board(uint8_t alternate_touch_address) {
  if (rtc_magic != kRtcMagic) {
    rtc_magic = kRtcMagic;
    rtc_attempt = 0;
  }

  // Touch addresses to try, the one that worked last first.
  uint8_t plan[2];
  uint32_t touch_attempts = 0;
  const uint8_t stored = load_touch_address();
  if (alternate_touch_address != 0) {
    const bool alt_first = stored == alternate_touch_address;
    plan[touch_attempts++] = alt_first ? alternate_touch_address : kDefaultAddress;
    plan[touch_attempts++] = alt_first ? kDefaultAddress : alternate_touch_address;
  } else {
    plan[touch_attempts++] = kDefaultAddress;
  }
  const uint32_t attempt = rtc_attempt % (touch_attempts + 1);
  const bool with_touch = attempt < touch_attempts;

  // Count this attempt before trying it: a driver that panics or trips the
  // watchdog must move the next boot on to the next option too.
  rtc_attempt = attempt + 1;

  // Always the default-config constructor: it builds the board's own
  // drivers directly. Board(const BoardConfig&) goes through the driver
  // factories, which this build leaves empty (no esp_panel_drivers_conf.h).
  Board* board = new Board();
  if (!with_touch) {
    ESP_LOGW(kTag, "Touch did not start; running without touch");
    // getConfig() is const only by interface: _config is a plain member,
    // and init() skips the touch when it holds none.
    const_cast<BoardConfig&>(board->getConfig()).touch.reset();
  }
#if CONFIG_IDF_TARGET_ESP32P4 && CONFIG_ESP_REV_MIN_FULL >= 300
  // ESP32_Display_Panel 1.0.4 completes a partial DSI host config with
  // MIPI_DSI_PHY_CLK_SRC_DEFAULT, which ESP-IDF 5.5 keeps as the pre-v3
  // PLL_F20M; v3 silicon has no such PHY PLL reference and
  // esp_lcd_new_dsi_bus() aborts. Hand the bus a full config with the v3
  // default instead.
  if (board->getConfig().lcd.has_value()) {
    auto* dsi = std::get_if<BusDSI::Config>(
        &const_cast<BoardConfig&>(board->getConfig()).lcd->bus_config);
    const auto* partial =
        dsi ? std::get_if<BusDSI::HostPartialConfig>(&dsi->host) : nullptr;
    if (partial) {
      const BusDSI::HostPartialConfig lanes = *partial;
      dsi->host = BusDSI::HostFullConfig{
          .bus_id = BusDSI::HOST_ID_DEFAULT,
          .num_data_lanes = static_cast<uint8_t>(lanes.num_data_lanes),
          .phy_clk_src = MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT,
          .lane_bit_rate_mbps = static_cast<float>(lanes.lane_bit_rate_mbps),
      };
    }
  }
#endif
  bool ok = board->init();
  if (ok && with_touch && plan[attempt] != kDefaultAddress) {
    static_cast<BusI2C*>(board->getTouch()->getBus())
        ->configI2C_Address(plan[attempt]);
  }
  if (!ok || !board->begin()) {
    // Nothing useful can run without the display: try the next option from
    // scratch (rtc_attempt already points at it).
    ESP_LOGE(kTag, "Display board start failed (attempt %u), restarting",
             (unsigned)attempt);
    delay(1000);
    esp_restart();
  }
  rtc_attempt = 0;
  if (with_touch) {
    ESP_LOGI(kTag, "Display and touch started (touch at 0x%02X)",
             (unsigned)plan[attempt]);
  } else {
    ESP_LOGI(kTag, "Display started, without touch");
  }
  if (with_touch && alternate_touch_address != 0 && plan[attempt] != stored) {
    save_touch_address(plan[attempt]);
  }
  return board;
}

}  // namespace skgauges
