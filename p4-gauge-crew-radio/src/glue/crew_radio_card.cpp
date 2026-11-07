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
#include <mutex>
#include <string>

#include "core/channel_crypto.h"
#include "core/clock_policy.h"
#include "core/hello.h"
#include "core/levels.h"
#include "core/mic.h"
#include "core/radio_settings.h"
#include "core/voice.h"
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    root["echo_cancel"] = echo_cancel;
    root["noise_suppress"] = noise_suppress;
    root["full_duplex"] = full_duplex;
    root["reticulum"] = reticulum;
    root["reticulum_node"] = reticulum_node.c_str();
    root["relay"] = relay;
    return true;
  }

  // The boot load and a web UI save. Everything is checked before anything
  // changes; a new channel key is derived and stored before the save is
  // accepted, so a save that returns has its key in place.
  bool from_json(const JsonObject& c) override {
    // The screen's level steps write the card from the loop task while the
    // web server's task saves it (step_volume, step_mic_gain): every field
    // under the lock, except while the key is derived (seconds).
    std::unique_lock<std::mutex> lock(mutex_);
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
    const bool new_echo = c["echo_cancel"] | echo_cancel;
    const int new_noise = c["noise_suppress"] | noise_suppress;
    const bool new_duplex = c["full_duplex"] | full_duplex;
    const bool new_reticulum = c["reticulum"] | reticulum;
    const std::string new_node = c["reticulum_node"].is<const char*>()
                                     ? c["reticulum_node"].as<const char*>()
                                     : reticulum_node;
    const bool new_relay = c["relay"] | relay;
    // A node is needed only with Reticulum on; one that is given must parse.
    std::string node_host;
    int node_port = 0;
    if ((new_reticulum || !new_node.empty()) &&
        !settings::parse_host_port(new_node, &node_host, &node_port)) {
      ESP_LOGW(kTag, "Crew radio card refused: the transport node is host, "
                     "host:port, [v6] or [v6]:port");
      return false;
    }
    uint32_t group_addr;
    if (!settings::parse_group(new_group, &group_addr) ||
        !settings::valid_port(new_port) ||
        new_hops < settings::kMaxHopsMin || new_hops > settings::kMaxHopsMax ||
        new_volume < 0 || new_volume > 100 ||
        !settings::valid_talk_gpio(new_gpio) || !(new_gain >= 0.0f) ||
        new_gain > mic::kMaxGainDb || new_noise < 0 ||
        new_noise > voice::kMaxNoiseSuppressDb) {
      ESP_LOGW(kTag, "Crew radio card refused: a value is out of range");
      return false;
    }
    std::string new_hint = key_hint;
    lock.unlock();  // The key's PBKDF2 must not hold the loop's level steps
    // Only from the web UI: to_json never writes the key, so one in the
    // file at boot is a file someone else wrote, and its PBKDF2 would run in
    // setup(), under the loop watchdog.
    if (loaded_ && c["channel_key"].is<const char*>()) {
      std::string key = c["channel_key"].as<const char*>();
      // This copy wiped when done (ArduinoJson's and the web server's
      // buffers are theirs to free).
      struct Wipe {
        std::string& s;
        ~Wipe() {
          if (!s.empty()) mbedtls_platform_zeroize(&s[0], s.size());
        }
      } wipe{key};
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
    lock.lock();
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
    echo_cancel = new_echo;
    noise_suppress = new_noise;
    full_duplex = new_duplex;
    reticulum = new_reticulum;
    reticulum_node = new_node;
    relay = new_relay;
    key_hint = new_hint;
    // A web UI save: the two levels and the mic's cleanup at once; the
    // rest from the next restart.
    if (loaded_) {
      esp::set_volume(volume);
      esp::set_mic_gain(mic_gain);
      esp::set_voice(echo_cancel, noise_suppress, full_duplex);
    }
    return true;
  }

  // After the boot load: from then on a save is the web UI's.
  void mark_loaded() { loaded_ = true; }

  // One step of the speaker volume or the mic gain (core/levels.h), from
  // the loop task: the new value, and whether it changed.
  int step_volume(int dir, bool* changed) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int v = levels::step_volume(volume, dir);
    *changed = v != volume;
    volume = v;
    return v;
  }
  float step_mic_gain(int dir, bool* changed) {
    std::lock_guard<std::mutex> lock(mutex_);
    const float db = levels::step_mic_gain(mic_gain, dir);
    *changed = db != mic_gain;
    mic_gain = db;
    return db;
  }

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
  bool echo_cancel = true;
  int noise_suppress = voice::kDefaultNoiseSuppressDb;  // dB, 0 = off
  bool full_duplex = false;
  bool reticulum = false;
  std::string reticulum_node;  // host[:port], 4242 when left out
  bool relay = true;

 private:
  bool loaded_ = false;
  std::mutex mutex_;
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

// Where Reticulum stands, in one line for the Status page.
std::string reticulum_line(const esp::RadioStatus& s) {
  const esp::ReticulumStatus& r = s.reticulum;
  char buf[200];
  switch (r.state) {
    case esp::ReticulumStatus::State::kOff:
      snprintf(buf, sizeof(buf), "Off%s%s", r.note.empty() ? "" : ": ",
               r.note.c_str());
      break;
    case esp::ReticulumStatus::State::kWaiting:
      snprintf(buf, sizeof(buf), "Waiting for the clock (or memory)");
      break;
    case esp::ReticulumStatus::State::kConnecting:
      snprintf(buf, sizeof(buf), "%s: %s", r.where.c_str(),
               r.note.empty() ? "connecting" : r.note.c_str());
      break;
    case esp::ReticulumStatus::State::kConnected:
      snprintf(buf, sizeof(buf),
               "%s: %d link%s, %d peer%s; %lu out, %lu in, %llu relayed; "
               "%lu reconnects",
               r.where.c_str(), r.links, r.links == 1 ? "" : "s", r.peers,
               r.peers == 1 ? "" : "s", (unsigned long)r.tx,
               (unsigned long)r.rx, (unsigned long long)s.relayed,
               (unsigned long)r.reconnects);
      break;
  }
  return buf;
}

// The card, for the screen's level steps as well as the web UI.
std::shared_ptr<RadioCard> g_card;
bool g_radio_on = false;  // The card said On at boot

// A level the screen stepped is written to the card's file this long after
// the last step, not on every tap: each save rewrites the file.
constexpr uint32_t kLevelSaveDelayMs = 3000;
bool g_level_dirty = false;
uint32_t g_level_changed_ms = 0;

void note_level_changed() {
  g_level_dirty = true;
  g_level_changed_ms = millis();
}

}  // namespace

bool step_volume(int dir) {
  if (!g_radio_on || g_card == nullptr) return false;
  bool changed = false;
  const int v = g_card->step_volume(dir, &changed);
  esp::set_volume(v);
  if (changed) {
    note_level_changed();
    ESP_LOGI(kTag, "Speaker volume %d (from the screen)", v);
  }
  return true;
}

bool step_mic_gain(int dir) {
  if (!g_radio_on || g_card == nullptr) return false;
  bool changed = false;
  const float db = g_card->step_mic_gain(dir, &changed);
  esp::set_mic_gain(db);
  if (changed) {
    note_level_changed();
    ESP_LOGI(kTag, "Mic gain %.1f dB (from the screen)", db);
  }
  return true;
}

void begin(int (*finger_count)()) {
  g_card = std::make_shared<RadioCard>();
  std::shared_ptr<RadioCard>& card = g_card;
  card->load();
  card->mark_loaded();
  sensesp::ConfigItem(card)
      ->set_title("Crew radio")
      ->set_description(
          "The Crew Radio intercom on this unit's speaker and microphone. "
          "Talk: hold two fingers on the screen, or a button on the 40-pin "
          "header (set its GPIO); 90 s of talk at once cuts the mic until "
          "it is released. Speaker volume and mic gain: also on the Crew "
          "page (tap - or + under the arc, the label between them for the "
          "mic); a save here sets both at once, and the echo cancelling, "
          "noise suppression and full duplex too. Channel key: the crew's "
          "key, as set on the phones; type it to set or change it (the save "
          "takes about 20 s: wait for it), leave it empty to keep the one "
          "saved. Group and port: as on the phones. Reticulum: the channel "
          "through a transport node (the boat's rnsd, or a hub ashore), for "
          "a phone off the boat's WLAN; with Relay on, this unit bridges "
          "the two. The rest takes effect when the unit restarts.")
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
  config.echo_cancel = card->echo_cancel;
  config.noise_suppress_db = card->noise_suppress;
  config.full_duplex = card->full_duplex;
  int node_port = settings::kDefaultReticulumPort;
  config.reticulum =
      card->reticulum && settings::parse_host_port(card->reticulum_node,
                                                   &config.reticulum_host,
                                                   &node_port);
  config.reticulum_port = (uint16_t)node_port;
  config.relay = card->relay;
  esp::begin(config);
  mbedtls_platform_zeroize(config.key, sizeof(config.key));
  if (!card->enabled) return;
  g_radio_on = true;

  // A level the screen stepped, into the card's file once the taps stop.
  sensesp::event_loop()->onRepeat(500, []() {
    if (g_level_dirty && millis() - g_level_changed_ms >= kLevelSaveDelayMs) {
      g_level_dirty = false;
      g_card->save();
    }
  });

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
  static auto* rns = card->reticulum
                         ? new sensesp::StatusPageItem<String>(
                               "Radio Reticulum", "-", "Crew radio", 2047)
                         : nullptr;
  // Both of the ES7210's channels as read, whichever the card uses: their
  // DC (a stuck line is -1 or 0 with no signal), signal RMS and peak, and
  // the frame's first samples in hex, to tell a quiet mic from no data.
  static auto* mic_line = new sensesp::StatusPageItem<String>(
      "Radio mic", "-", "Crew radio", 2046);
  // The mic's cleanup: what is on, what the echo canceller took out, and
  // what it costs a frame (of the 20 ms each has).
  static auto* voice_line = new sensesp::StatusPageItem<String>(
      "Radio voice", "-", "Crew radio", 2048);
  sensesp::event_loop()->onRepeat(1000, []() {
    const esp::RadioStatus s = esp::status();
    state->set(esp::state_text(s.state));
    crew->set(String((int)s.members.size()) + ": " +
              crew_list(s.members).c_str());
    clock->set(esp::clock_text(s.clock));
    char buf[200];
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
    snprintf(buf, sizeof(buf),
             "MIC1 rms %d peak %d; MIC2 rms %d peak %d; TDM slots rms "
             "%d/%d/%d/%d (MIC1/ref/MIC2/MIC4); raw %04x %04x|%04x %04x|"
             "%04x %04x|%04x %04x; gain %.1f dB",
             s.mic_stats[0].ac_rms, s.mic_stats[0].peak, s.mic_stats[1].ac_rms,
             s.mic_stats[1].peak, s.mic_slot_rms[0], s.mic_slot_rms[1],
             s.mic_slot_rms[2], s.mic_slot_rms[3], s.mic_raw[0], s.mic_raw[1],
             s.mic_raw[2], s.mic_raw[3], s.mic_raw[4], s.mic_raw[5],
             s.mic_raw[6], s.mic_raw[7], s.mic_gain_db);
    mic_line->set(buf);
    if (!s.mic_ok) {
      voice_line->set("No microphone");
    } else if (!s.voice_ok) {
      voice_line->set("Off: no memory for it (the mic goes out as heard)");
    } else {
      char echo[48];
      if (!s.echo_cancel) {
        snprintf(echo, sizeof(echo), "off");
      } else if (s.echo_db < 0) {
        snprintf(echo, sizeof(echo), "on (the speaker silent)");
      } else {
        snprintf(echo, sizeof(echo), "on, %d dB taken out", s.echo_db);
      }
      snprintf(buf, sizeof(buf),
               "Echo cancelling %s; noise suppression %s%d dB; %s duplex; "
               "%d us a frame (%d at most)",
               echo, s.noise_suppress_db > 0 ? "" : "off, ",
               s.noise_suppress_db, s.full_duplex ? "full" : "half",
               s.voice_avg_us, s.voice_us);
      voice_line->set(buf);
    }
    if (rns != nullptr) rns->set(reticulum_line(s).c_str());
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
         R"J("key_beeps":{"title":"Key beeps","type":"boolean","description":"A beep when the mic keys and un-keys (the time-out always beeps)."},)J"
         R"J("echo_cancel":{"title":"Echo cancelling","type":"boolean","description":"Takes what this unit's own speaker plays out of the mic, from the board's echo reference (speexdsp). Needed for full duplex; on a half-duplex unit it takes out the key beeps."},)J"
         R"J("noise_suppress":{"title":"Noise suppression (dB)","type":"integer","minimum":0,"maximum":30,"description":"How far steady noise (an engine, a fan, wind hiss) is turned down in the mic; 0 is off. 15 is a moderate setting; more can make the voice sound watery."},)J"
         R"J("full_duplex":{"title":"Full duplex","type":"boolean","description":"Play the crew while this unit talks, as a phone call. Use with echo cancelling on, or the crew hears itself. Off: the speaker is silent while talking."},)J"
         R"J("reticulum":{"title":"Reticulum","type":"boolean","description":"The channel through a Reticulum transport node as well, for the crew off the boat's WLAN (the phones' Reticulum tile)."},)J"
         R"J("reticulum_node":{"title":"Transport node","type":"string","description":"host, host:port, [v6] or [v6]:port of the node's TCP server interface; 4242 when no port is given. As on the phones."},)J"
         R"J("relay":{"title":"Relay","type":"boolean","description":"With Reticulum on, bridge it and the WLAN, so the phones on board hear the crew ashore and back."})J"
         R"J(}})J";
}

}  // namespace sensesp

#endif
