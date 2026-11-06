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
//            buffer). Half duplex: the crew is not played while keyed.
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
  int mic_channel = 0;   // 0 = MIC1, 1 = MIC2
  bool key_beeps = false;
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
  uint64_t sent = 0;
  int timeouts = 0;
  int mic_level = 0;  // RMS of the last frame heard by the mic
  uint64_t relayed = 0;
  ReticulumStatus reticulum;
  // For the screen (the 4"'s Crew page and halo).
  std::string channel;
  int volume = 0;  // 0..100, the card's
};

// From setup(), after the board (and so the I2C driver) is up.
void begin(const RadioConfig& config);
// A Signal K navigation.datetime as it arrived (from the loop task).
void note_gnss_time(int64_t epoch_s);
// Two fingers on the screen, or not (from the loop task, every touch read).
void set_touch_talk(bool held);
RadioStatus status();

}  // namespace esp
}  // namespace crewradio

#endif
