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

TEST(settings_a_reticulum_node_as_the_app_takes_it) {
  std::string host;
  int port = 0;
  auto is = [&](const char* text, const char* h, int p) {
    return parse_host_port(text, &host, &port) && host == h && port == p;
  };
  CHECK(is(" hub.example.org ", "hub.example.org", 4242));
  CHECK(is("192.168.1.9:4965", "192.168.1.9", 4965));
  CHECK(is("[::1]", "::1", 4242));
  CHECK(is("[fe80::1]:37428", "fe80::1", 37428));
  CHECK(is("2001:db8::7", "2001:db8::7", 4242));
  for (const char* bad : {"", "hub:0", "hub:65536", "hub:042", "hub:", ":4242",
                          "two words", "-hub", "[::1]x", "[hub]:4242", "[]",
                          "tcp://hub:4242"}) {
    CHECK(!parse_host_port(bad, &host, &port));
  }
}

TEST(settings_an_ipv6_node_must_be_an_ipv6_address) {
  std::string host;
  int port = 0;
  for (const char* good : {"::1", "::", "fe80::1", "2001:db8::7",
                           "2001:db8:0:0:0:0:0:1", "1:2:3:4:5:6:7:8",
                           "::ffff:192.168.1.9", "64:ff9b::10.0.0.1",
                           "[fe80::1]:37428", "1::", "[1:2:3:4:5:6:7::]"}) {
    CHECK(parse_host_port(good, &host, &port));
  }
  for (const char* bad : {"1.2.3.4:5:6", "::::", ":::", "1:::2", "1::2::3",
                          ":1:2:3:4:5:6:7", "1:2:3:4:5:6:7:", "1:2:3:4:5:6:7:8:9",
                          "1:2:3:4:5:6:7::8", "12345::1", "::1.2.3",
                          "::256.1.1.1", "::1.2.3.4:5", "1.2.3.4::",
                          "[1.2.3.4:5:6]", "[::::]:4242", "[fe80::1%eth0]"}) {
    CHECK(!parse_host_port(bad, &host, &port));
  }
}
