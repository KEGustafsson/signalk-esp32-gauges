// SPDX-License-Identifier: EUPL-1.2
#pragma once

// The radio on the 4": three FreeRTOS tasks around the core.
//
//   cr-net   (core 0) owns the UDP socket and NetNode: receives, admits,
//            learns peers, keeps the roster, sends the hello every second,
//            hands audio to cr-audio through a lock-free ring and publishes
//            where this node's voice goes (core/peer_snapshot.h).
//   cr-audio (core 0) owns the decoders, the Playout, the Mixer and, for
//            talking, the talk key, the encoder and the Transmitter: each
//            20 ms it writes one frame to I2S and reads one from the mic
//            (both block on the DMA, so the I2S clock paces it), and while
//            keyed sends the frame to the peers cr-net publishes. Every
//            libopus call is made here (the build's scratch is one global
//            buffer). Half duplex unless set otherwise: the crew is not
//            played while keyed. Every mic frame goes through the echo
//            canceller and noise suppression (core/voice.h) against the
//            board's echo reference. It also sets the codecs' levels
//            (set_volume, set_mic_gain).
//   cr-rns   (core 0, below both; phase 2b, when switched on) owns the
//            TCP connection to a Reticulum transport node and the links in
//            it (esp/reticulum_link.h); the channel's packets go to and from
//            cr-net through two rings, so Ingress still has one caller, and
//            cr-net bridges the WLAN and Reticulum (core/net_node.h).
//   cr-beat  (core 0, low) owns the ClockPolicy, the MemoryGuard and the
//            Supervisor, once a second; restarts a stalled task, never the
//            unit; after three restarts in ten minutes leaves the radio off.
//
// Core 0, away from Arduino's loop task (core 1): a 5 s SensESP call to the
// C6 never reaches the audio. The caches of the receive pipeline live for
// the process, through a task restart, so a replay does not survive one.
// Talk keys: a button on the 40-pin header (to ground, active low) and a
// two-finger hold on the screen (set_touch_talk() from the loop task), with
// a 90 s time-out (core/talk_key.h).

#if defined(ESP_PLATFORM)

#include <stdint.h>

#include <string>
#include <vector>

#include "core/clock_policy.h"
#include "core/memory_guard.h"
#include "core/mic.h"
#include "core/radio_state.h"
#include "core/roster.h"
#include "esp/reticulum_link.h"

namespace crewradio {
namespace esp {

struct RadioConfig {
  bool enabled = false;
  bool has_key = false;
  uint8_t key[32] = {};  // The packet key (never the channel key)
  std::string name;
  std::string channel;   // The channel (boat) name, for the screen only
  uint32_t group = 0;    // Host byte order
  uint16_t port = 0;
  int max_hops = 4;
  int volume = 70;       // 0..100
  // Talking (phase 2).
  int talk_gpio = -1;    // The button's GPIO, -1 for none
  bool touch_talk = true;  // A two-finger hold keys the mic
  bool opus = true;      // Opus (else PCM16)
  float mic_gain_db = 30.0f;
  int mic_channel = 2;   // 0 = MIC1, 1 = MIC2, 2 = both averaged (mic::kBoth)
  bool key_beeps = false;
  // The mic's cleanup (core/voice.h) and whether the crew is played while
  // this unit talks; all three also live (set_voice).
  bool echo_cancel = true;
  int noise_suppress_db = 15;  // 0 = off
  bool full_duplex = false;
  // Reticulum (phase 2b).
  bool reticulum = false;
  std::string reticulum_host;  // The transport node
  uint16_t reticulum_port = 4242;
  bool relay = true;  // Bridge the WLAN and Reticulum
};

const char* state_text(RadioState s);
const char* clock_text(ClockSource s);
const char* shed_text(Shed s);

struct RadioStatus {
  RadioState state = RadioState::kOff;
  ClockSource clock = ClockSource::kNone;
  Shed shed = Shed::kNone;
  std::vector<Member> members;
  uint64_t received = 0, rejected = 0, stale = 0, duplicates = 0, late = 0;
  uint64_t audio = 0, ring_drops = 0, frames = 0, decode_errors = 0;
  uint64_t concealed = 0;
  uint32_t free_dma = 0;
  int restarts = 0;
  bool socket_open = false;
  bool mic_ok = false;
  bool talking = false;
  // The talk key held but kept silent: pressed while another talker was
  // heard, or yielded to one (core/channel_floor.h). The screen says
  // "Channel busy".
  bool busy = false;
  uint64_t sent = 0;
  int timeouts = 0;
  int mic_level = 0;  // RMS of the last frame heard by the mic
  // Both ES7210 channels of that frame (MIC1 left, MIC2 right) and its
  // first stereo samples as read, for the Status page's mic diagnostics.
  mic::ChannelStats mic_stats[2];
  uint16_t mic_raw[8] = {};  // Each TDM slot's first two samples
  int mic_slot_rms[mic::kTdmSlots] = {};  // MIC1, echo ref, MIC2, MIC4
  // The mic's cleanup: what the echo canceller took out over the last
  // second the speaker played (dB; -1: it played nothing, or the canceller
  // is off), and the longest it took over a frame (microseconds).
  int echo_db = -1;
  int voice_us = 0;
  int voice_avg_us = 0;  // Its mean over that second
  bool voice_ok = false;  // speexdsp's states allocated
  uint64_t relayed = 0;
  ReticulumStatus reticulum;
  // When cr-beat last wrote this (esp_timer ms): a screen that sees it old
  // knows the radio stopped telling it anything, and says so.
  int64_t beat_ms = 0;
  // For the screen (the 4"'s Crew page and halo).
  std::string channel;
  // The levels asked for (the card's, or set since: set_volume,
  // set_mic_gain), not a reading of the codecs.
  int volume = 0;           // 0..100
  float mic_gain_db = 0.0f;  // 0..37.5, as the card has it
  int mic_channel = 0;       // The mic in use (RadioConfig's, from begin())
  bool echo_cancel = false;
  int noise_suppress_db = 0;
  bool full_duplex = false;
};

// From setup(), after the board (and so the I2C driver) is up.
void begin(const RadioConfig& config);
// A Signal K navigation.datetime as it arrived (from the loop task).
void note_gnss_time(int64_t epoch_s);
// Two fingers on the screen, or not (from the loop task, every touch read).
void set_touch_talk(bool held);
// The speaker volume (0..100, 0 mutes) and the mic gain (dB, 0..37.5),
// from any task: cr-audio, which owns the codecs, sets them within a frame
// (20 ms); a radio that is not running takes them when its audio starts.
void set_volume(int volume);
void set_mic_gain(float db);
// The mic's echo canceller, its noise suppression (dB, 0 = off) and full
// duplex (the crew played while talking), from any task: cr-audio takes
// them at its next frame; full duplex at the next key.
void set_voice(bool echo_cancel, int noise_suppress_db, bool full_duplex);
RadioStatus status();

}  // namespace esp
}  // namespace crewradio

#endif
