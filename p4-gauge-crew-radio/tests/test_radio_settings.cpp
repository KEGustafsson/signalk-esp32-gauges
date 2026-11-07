// SPDX-License-Identifier: EUPL-1.2
// The settings card's rules, as the app's SettingsRules.
#include "core/radio_settings.h"
#include "test.h"

using namespace crewradio::settings;

TEST(settings_channel_key_rule_is_the_apps) {
  CHECK(valid_channel_key("abcd-efgh-jkmn"));
  CHECK(valid_channel_key(std::string(64, 'k')));
  CHECK(!valid_channel_key("short-key-1"));       // 11
  CHECK(!valid_channel_key(std::string(65, 'k')));
  CHECK(!valid_channel_key(" abcd-efgh-jkmn"));  // A space at an end
  CHECK(!valid_channel_key("abcd-efgh-jkmn "));
  CHECK(valid_channel_key("abcd efgh jkmn"));     // Inside is fine
  CHECK(!valid_channel_key("abcd-efgh-\tjkmn"));
  CHECK(!valid_channel_key("Pohjant\xC3\xA4hti-2026"));  // Printable ASCII only
}

TEST(settings_group_is_a_plain_ipv4_multicast_address) {
  uint32_t g = 0;
  CHECK(parse_group("239.255.42.1", &g) && g == 0xEFFF2A01u);
  CHECK(parse_group("224.0.0.0", &g) && g == 0xE0000000u);
  CHECK(!parse_group("192.168.1.1", &g));   // Not multicast
  CHECK(!parse_group("240.0.0.1", &g));
  CHECK(!parse_group("239.255.042.1", &g)); // Leading zero
  CHECK(!parse_group("239.256.42.1", &g));
  CHECK(!parse_group("239.255.42", &g));
  CHECK(!parse_group("239.255.42.1.5", &g));
  CHECK(!parse_group("239.255.42.1 ", &g));
  CHECK(!parse_group("", &g));
  CHECK(!parse_group("+239.255.42.1", &g));
}

TEST(settings_port_and_mask) {
  CHECK(valid_port(47474) && valid_port(1024) && valid_port(65535));
  CHECK(!valid_port(1023) && !valid_port(65536));
  CHECK(mask_key("abcd-efgh-jkmn").size() > 4);
  CHECK(mask_key("abcd-efgh-jkmn").substr(mask_key("abcd-efgh-jkmn").size() - 4) == "jkmn");
  CHECK(mask_key("abc").empty());
}

TEST(settings_talk_gpio_avoids_the_boards_own_pins) {
  CHECK(valid_talk_gpio(-1) && valid_talk_gpio(0) && valid_talk_gpio(54));
  CHECK(valid_talk_gpio(32) && valid_talk_gpio(45));
  CHECK(!valid_talk_gpio(-2) && !valid_talk_gpio(55));
  for (int taken : {7, 8, 9, 10, 11, 12, 13, 24, 25, 26, 27, 39, 44, 53}) {
    CHECK(!valid_talk_gpio(taken));
  }
}
