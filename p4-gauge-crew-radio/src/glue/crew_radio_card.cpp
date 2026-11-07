// SPDX-License-Identifier: EUPL-1.2
#if defined(ESP_PLATFORM)
#include "crew_radio_card.h"

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/platform_util.h>

#include <string.h>

#include <memory>
#include <string>

#include "core/channel_crypto.h"
#include "core/clock_policy.h"
#include "core/hello.h"
#include "core/mic.h"
#include "core/radio_settings.h"
#include "esp/radio_runtime.h"
#include "sensesp/signalk/signalk_value_listener.h"
#include "sensesp/system/lambda_consumer.h"
#include "sensesp/system/saveable.h"
#include "sensesp/ui/config_item.h"
#include "sensesp/ui/status_page_item.h"
#include "sensesp_base_app.h"

namespace crewradio {
namespace glue {

namespace {

constexpr const char* kTag = "radio";
constexpr const char* kNvsNamespace = "crewradio";
constexpr const char* kNvsKey = "pk";

bool load_packet_key(uint8_t key[32]) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, true)) return false;
  const bool ok = prefs.getBytesLength(kNvsKey) == 32 &&
                  prefs.getBytes(kNvsKey, key, 32) == 32;
  prefs.end();
  return ok;
}

bool store_packet_key(const uint8_t key[32]) {
  Preferences prefs;
  if (!prefs.begin(kNvsNamespace, false)) return false;
  const bool ok = prefs.putBytes(kNvsKey, key, 32) == 32;
  prefs.end();
  return ok;
}

}  // namespace

class RadioCard : public sensesp::FileSystemSaveable {
 public:
  RadioCard() : sensesp::FileSystemSaveable("/crewradio") {}

  bool to_json(JsonObject& root) override {
    root["enabled"] = enabled;
    root["name"] = name.c_str();
    root["channel"] = channel.c_str();
    root["channel_key"] = "";  // Never the key: only a new one is typed here
    root["key"] = key_hint.empty() ? "Not set" : key_hint.c_str();
    root["group"] = group.c_str();
    root["port"] = port;
    root["max_hops"] = max_hops;
    root["volume"] = volume;
    root["talk_gpio"] = talk_gpio;
    root["touch_talk"] = touch_talk;
    root["opus"] = opus;
    root["mic_gain"] = mic_gain;
    root["mic_channel"] = mic_channel == 1 ? "MIC2" : "MIC1";
    root["key_beeps"] = key_beeps;
    return true;
  }

  // The boot load and a web UI save. Everything is checked before anything
  // changes; a new channel key is derived and stored before the save is
  // accepted, so a save that returns has its key in place.
  bool from_json(const JsonObject& c) override {
    const bool new_enabled = c["enabled"] | enabled;
    std::string new_name = c["name"].is<const char*>()
                               ? sanitise_name(c["name"].as<const char*>())
                               : name;
    new_name.resize(utf8_prefix_len(new_name, Hello::kMaxNameBytes));
    std::string new_channel = c["channel"].is<const char*>()
                                  ? sanitise_name(c["channel"].as<const char*>())
                                  : channel;
    new_channel.resize(utf8_prefix_len(new_channel, settings::kNameMax));
    const std::string new_group =
        c["group"].is<const char*>() ? c["group"].as<const char*>() : group;
    const int new_port = c["port"] | port;
    const int new_hops = c["max_hops"] | max_hops;
    const int new_volume = c["volume"] | volume;
    const int new_gpio = c["talk_gpio"] | talk_gpio;
    const bool new_touch = c["touch_talk"] | touch_talk;
    const bool new_opus = c["opus"] | opus;
    const float new_gain = c["mic_gain"] | mic_gain;
    int new_mic = mic_channel;
    if (c["mic_channel"].is<const char*>()) {
      const char* m = c["mic_channel"].as<const char*>();
      if (strcmp(m, "MIC1") == 0) {
        new_mic = 0;
      } else if (strcmp(m, "MIC2") == 0) {
        new_mic = 1;
      } else {
        ESP_LOGW(kTag, "Crew radio card refused: no microphone %s", m);
        return false;
      }
    }
    const bool new_beeps = c["key_beeps"] | key_beeps;
    uint32_t group_addr;
    if (!settings::parse_group(new_group, &group_addr) ||
        !settings::valid_port(new_port) ||
        new_hops < settings::kMaxHopsMin || new_hops > settings::kMaxHopsMax ||
        new_volume < 0 || new_volume > 100 ||
        !settings::valid_talk_gpio(new_gpio) || !(new_gain >= 0.0f) ||
        new_gain > mic::kMaxGainDb) {
      ESP_LOGW(kTag, "Crew radio card refused: a value is out of range");
      return false;
    }
    std::string new_hint = key_hint;
    if (c["channel_key"].is<const char*>()) {
      const std::string key = c["channel_key"].as<const char*>();
      if (!key.empty()) {
        if (!settings::valid_channel_key(key)) {
          ESP_LOGW(kTag, "Channel key refused: 12-64 printable ASCII, no "
                         "space at either end");
          return false;
        }
        uint8_t packet_key[32];
        // Seconds of PBKDF2 in the web server's task: pause now and then
        // so the idle task (and its watchdog) keeps running.
        const bool derived = ChannelCrypto::derive(
            (const uint8_t*)key.data(), key.size(), packet_key,
            ChannelCrypto::kIterations, [] { vTaskDelay(1); });
        const bool stored = derived && store_packet_key(packet_key);
        mbedtls_platform_zeroize(packet_key, sizeof(packet_key));
        if (!stored) {
          ESP_LOGE(kTag, "The channel key could not be derived or stored");
          return false;
        }
        new_hint = settings::mask_key(key);
        ESP_LOGI(kTag, "Channel key saved (from the next restart)");
      }
    }
    if (!loaded_ && c["key"].is<const char*>()) {
      // The boot load: the hint as it was saved ("Not set" means none). A
      // web UI save sends the read-only field back; it is not taken.
      const std::string saved = c["key"].as<const char*>();
      if (saved != "Not set") new_hint = saved;
    }
    enabled = new_enabled;
    name = new_name;
    channel = new_channel;
    group = new_group;
    port = new_port;
    max_hops = new_hops;
    volume = new_volume;
    talk_gpio = new_gpio;
    touch_talk = new_touch;
    opus = new_opus;
    mic_gain = new_gain;
    mic_channel = new_mic;
    key_beeps = new_beeps;
    key_hint = new_hint;
    return true;
  }

  // After the boot load: from then on a save is the web UI's.
  void mark_loaded() { loaded_ = true; }

  bool enabled = false;
  std::string name;
  std::string channel;  // The boat's name on the Crew page; empty: "CREW"
  std::string key_hint;
  std::string group = settings::kDefaultGroup;
  int port = settings::kDefaultPort;
  int max_hops = 4;
  int volume = 70;
  int talk_gpio = -1;
  bool touch_talk = true;
  bool opus = true;
  float mic_gain = 30.0f;
  int mic_channel = 0;
  bool key_beeps = false;

 private:
  bool loaded_ = false;
};

}  // namespace glue
}  // namespace crewradio

namespace sensesp {
const String ConfigSchema(const crewradio::glue::RadioCard&);
}  // namespace sensesp

namespace crewradio {
namespace glue {

namespace {

std::string crew_list(const std::vector<Member>& members) {
  std::string s;
  for (const Member& m : members) {
    if (!s.empty()) s += ", ";
    s += m.name.empty() ? "(no hello yet)" : m.name;
    if (m.talking) s += " (talking)";
  }
  return s.empty() ? "-" : s;
}

}  // namespace

void begin(int (*finger_count)()) {
  static std::shared_ptr<RadioCard> card = std::make_shared<RadioCard>();
  card->load();
  card->mark_loaded();
  sensesp::ConfigItem(card)
      ->set_title("Crew radio")
      ->set_description(
          "The Crew Radio intercom on this unit's speaker and microphone. "
          "Talk: hold two fingers on the screen, or a button on the 40-pin "
          "header (set its GPIO); 90 s of talk at once cuts the mic until "
          "it is released. Channel key: the crew's key, as set on the "
          "phones; type it to set or change it (the save takes a few "
          "seconds), leave it empty to keep the one saved. Group and port: "
          "as on the phones. Takes effect when the unit restarts.")
      ->set_requires_restart(true)
      ->set_sort_order(97);

  esp::RadioConfig config;
  config.enabled = card->enabled;
  config.has_key = load_packet_key(config.key);
  config.name = card->name.empty()
                    ? std::string(sensesp::SensESPBaseApp::get_hostname().c_str())
                    : card->name;
  config.channel = card->channel;
  settings::parse_group(card->group, &config.group);
  config.port = (uint16_t)card->port;
  config.max_hops = card->max_hops;
  config.volume = card->volume;
  config.talk_gpio = card->talk_gpio;
  config.touch_talk = card->touch_talk && finger_count != nullptr;
  config.opus = card->opus;
  config.mic_gain_db = card->mic_gain;
  config.mic_channel = card->mic_channel;
  config.key_beeps = card->key_beeps;
  esp::begin(config);
  mbedtls_platform_zeroize(config.key, sizeof(config.key));
  if (!card->enabled) return;

  // The two-finger hold: the touch port is read on this (the loop) task.
  if (config.touch_talk) {
    static int (*fingers)() = finger_count;
    sensesp::event_loop()->onRepeat(
        20, []() { esp::set_touch_talk(fingers() >= 2); });
  }

  // The boat's GNSS time, for the clock (clock_policy.h decides whether it
  // is live). Not a numeric reading, so not a GaugeListener.
  auto* datetime =
      new sensesp::SKValueListener<String>("navigation.datetime", 1000);
  datetime->connect_to(new sensesp::LambdaConsumer<String>([](String v) {
    int64_t s;
    if (parse_iso8601_utc(v.c_str(), &s)) esp::note_gnss_time(s);
  }));

  static auto* state = new sensesp::StatusPageItem<String>(
      "Radio", "Starting", "Crew radio", 2000);
  static auto* crew = new sensesp::StatusPageItem<String>(
      "Radio crew", "-", "Crew radio", 2010);
  static auto* clock = new sensesp::StatusPageItem<String>(
      "Radio clock", "None", "Crew radio", 2020);
  static auto* packets = new sensesp::StatusPageItem<String>(
      "Radio packets", "-", "Crew radio", 2030);
  static auto* audio = new sensesp::StatusPageItem<String>(
      "Radio audio", "-", "Crew radio", 2040);
  static auto* memory = new sensesp::StatusPageItem<String>(
      "Radio memory", "-", "Crew radio", 2050);
  static auto* talk = new sensesp::StatusPageItem<String>(
      "Radio talk", "-", "Crew radio", 2045);
  sensesp::event_loop()->onRepeat(1000, []() {
    const esp::RadioStatus s = esp::status();
    state->set(esp::state_text(s.state));
    crew->set(String((int)s.members.size()) + ": " +
              crew_list(s.members).c_str());
    clock->set(esp::clock_text(s.clock));
    char buf[160];
    snprintf(buf, sizeof(buf),
             "%llu in, %llu rejected, %llu stale, %llu copies, %llu late",
             (unsigned long long)s.received, (unsigned long long)s.rejected,
             (unsigned long long)s.stale, (unsigned long long)s.duplicates,
             (unsigned long long)s.late);
    packets->set(buf);
    snprintf(buf, sizeof(buf),
             "%llu frames played, %llu concealed, %llu decode errors, "
             "%llu dropped (ring)",
             (unsigned long long)s.frames, (unsigned long long)s.concealed,
             (unsigned long long)s.decode_errors,
             (unsigned long long)s.ring_drops);
    audio->set(buf);
    snprintf(buf, sizeof(buf),
             "%s; %llu frames sent; mic level %d; %d time-outs%s",
             s.talking ? "Talking" : "Listening", (unsigned long long)s.sent,
             s.mic_level, s.timeouts, s.mic_ok ? "" : "; no microphone");
    talk->set(buf);
    snprintf(buf, sizeof(buf), "%u bytes of DMA RAM free; %s; %d restarts",
             (unsigned)s.free_dma, esp::shed_text(s.shed), s.restarts);
    memory->set(buf);
  });
}

}  // namespace glue
}  // namespace crewradio

namespace sensesp {

const String ConfigSchema(const crewradio::glue::RadioCard&) {
  return R"J({"type":"object","properties":{)J"
         R"J("enabled":{"title":"On","type":"boolean","description":"Listen to the crew's channel on this unit's speaker."},)J"
         R"J("name":{"title":"Name on the roster","type":"string","description":"How the phones list this unit, up to 32 bytes. Empty: the unit's hostname."},)J"
         R"J("channel":{"title":"Channel name","type":"string","description":"The boat's or crew's name, shown on the Crew page (as the phones' Channel name). Empty: CREW."},)J"
         R"J("channel_key":{"title":"New channel key","type":"string","format":"password","description":"The crew's channel key, exactly as on the phones (12-64 printable ASCII). Leave empty to keep the saved one."},)J"
         R"J("key":{"title":"Saved key","type":"string","readOnly":true,"description":"The last four characters of the saved key."},)J"
         R"J("group":{"title":"Multicast group","type":"string","description":"As on the phones; 239.255.42.1 unless the crew changed it."},)J"
         R"J("port":{"title":"UDP port","type":"integer","minimum":1024,"maximum":65535},)J"
         R"J("max_hops":{"title":"Hop limit","type":"integer","minimum":1,"maximum":8},)J"
         R"J("volume":{"title":"Speaker volume","type":"integer","minimum":0,"maximum":100,"description":"100 is the codec's 0 dB; each step below is 0.5 dB; 0 is mute."},)J"
         R"J("touch_talk":{"title":"Two-finger talk","type":"boolean","description":"Hold two fingers on the screen to talk."},)J"
         R"J("talk_gpio":{"title":"Talk button GPIO","type":"integer","minimum":-1,"maximum":54,"description":"A push button from this GPIO on the 40-pin header to ground; -1 for none. Pins the board already uses are refused."},)J"
         R"J("opus":{"title":"Opus","type":"boolean","description":"Send Opus (about 60 bytes a frame); off sends PCM (640). The phones play either."},)J"
         R"J("mic_gain":{"title":"Mic gain (dB)","type":"number","minimum":0,"maximum":37.5,"description":"The ES7210's preamp: 3 dB steps to 30, then 34.5, 36, 37.5."},)J"
         R"J("mic_channel":{"title":"Microphone","type":"array","format":"select","uniqueItems":true,"items":{"type":"string","enum":["MIC1","MIC2"]},"description":"Which of the board's two microphones is used."},)J"
         R"J("key_beeps":{"title":"Key beeps","type":"boolean","description":"A beep when the mic keys and un-keys (the time-out always beeps)."})J"
         R"J(}})J";
}

}  // namespace sensesp

#endif
