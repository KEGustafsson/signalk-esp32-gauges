# P4 Gauge Crew Radio

A [Crew Radio](https://github.com/KEGustafsson/crew-radio) node for the 4"
gauge (Waveshare ESP32-P4-WIFI6-Touch-LCD-4C): the boat's intercom on the
same board that shows the instruments, through its ES8311 codec and NS4150B
speaker amplifier (and, from phase 2, its ES7210 microphone ADC). It speaks
the crew-radio app's wire format (v4) unchanged, so the phones, the Signal K
plugin and this node are one channel. Over the boat's WLAN, and over
Reticulum (phase 2b) for the crew ashore or on another boat, with the unit
bridging the two.

Nothing here commands a machine, and the radio is a guest on a gauge: it may
never take the display down, cover a reading or keep the unit from
restarting when its own rules say so (`AGENTS.md`). It is off until it is
switched on in its card; a gauge that never switches it on runs as before.

| | |
|---|---|
| **Status** | Phases 1, 2, 2b and 3 written: the 4" firmware listens to the channel, is listed on the phones' roster, talks (two-finger hold or a button), reaches the crew over Reticulum and bridges it to the WLAN, and shows the crew on the screen. Built for the board; not yet run on one (see "First power-up") |
| **Licence** | EUPL-1.2, as the whole repository and crew-radio, from which the core is ported (`LICENSE` here too, for the library on its own) |
| **Tests** | `tests/`: 176 host tests plus 3 with libopus, the app's own vectors (the channel's and Reticulum's) included |

## What runs where

```
p4-gauge-crew-radio/
  library.json      PlatformIO library "P4GaugeCrewRadio", linked into the 4"
                    firmware (waveshare-esp32-p4-wifi6-touch-lcd-4c)
  src/core/         pure C++17 + mbedTLS (+ libopus), host-tested: the wire,
                    the crypto, the receive pipeline, peers, roster, mixer,
                    decoders, clock, supervisor, memory guard, self-test
  src/port/         what the core needs from the platform: random bytes
                    (on the P4 an HMAC-DRBG seeded from the hardware RNG
                    with its ADC entropy source on: the P4 has no radio of
                    its own to make esp_fill_random() true random)
  src/esp/          ESP-IDF: the ES8311, I2S, the socket, the FreeRTOS tasks
  src/glue/         SensESP: the "Crew radio" card, its status lines, the
                    GNSS time listener
  tests/            host tests; vectors/ copied from crew-radio
```

There is no separate station firmware: this repository builds one firmware
per board (a second env compiles everything again, and what a unit is lives
in its cards, `AGENTS.md`). A 4" switched to the radio with only the Crew
page on it (phase 3) is the station. The display side is drawn as mock-ups
in `common/src/ui` (`radio_halo.*`, `crew_page.*`, `crew_view.h`) and
rendered by `tools/preview` (`out/radio*`).

### SensESP stays

`src/core` and `src/esp` do not use SensESP, so the core is tested on a PC.
The firmware keeps SensESP for what it does for the gauges: WiFi, OTA, the
web UI with the radio's card and status lines, and the Signal K connection,
which also brings the boat's GNSS time for the radio's clock (`src/glue`).

## Using it

In the unit's web UI, Configuration, **Crew radio**:

| Field | |
|---|---|
| **On** | Listen to the crew's channel on this unit's speaker |
| **Name on the roster** | How the phones list the unit; empty = its hostname |
| **Channel name** | The boat's or crew's name on the Crew page, as the phones' Channel name; empty = CREW |
| **New channel key** | The crew's key, exactly as on the phones (12-64 printable ASCII). Save takes a few seconds (the packet key is derived then); empty keeps the saved one. The key itself is never stored, only the packet key (NVS) and its last four characters (shown as **Saved key**) |
| **Multicast group**, **UDP port** | As on the phones: 239.255.42.1 and 47474 unless the crew changed them |
| **Hop limit** | 1-8, default 4 |
| **Speaker volume** | 100 = the codec's 0 dB, 0.5 dB a step below, 0 = mute |
| **Two-finger talk** | Hold two fingers on the screen to talk (on by default) |
| **Talk button GPIO** | A push button from this GPIO on the 40-pin header to ground (internal pull-up); -1 for none. The pins the board already uses are refused |
| **Opus** | Send Opus (about 60 bytes a frame, the default) or PCM (640); the phones play either |
| **Mic gain (dB)** | The ES7210's preamp, 0-37.5 (default 30) |
| **Microphone** | MIC1 or MIC2 of the board's two |
| **Key beeps** | A beep when the mic keys and un-keys; the time-out always beeps |
| **Reticulum** | The channel through a Reticulum transport node as well (off by default), as the phones' Reticulum tile |
| **Transport node** | `host`, `host:port`, `[v6]` or `[v6]:port` of the node's TCP server interface (the boat's rnsd, or a hub ashore; crew-radio's `docs/RETICULUM_HUB.md`); 4242 when no port is given |
| **Relay** | With Reticulum on, bridge it and the WLAN (on by default): the phones on board hear the crew ashore, and back |

Talking is push to talk: the mic is keyed while the fingers or the button
are held (40 ms to key, 60 ms to let go), and the unit is half duplex: the
crew is not played while it talks. **90 s of talk at once cuts the mic**,
with three low beeps, and it stays cut until everything is let go: a button
shorted by spray or a hand resting on the screen would otherwise hold the
channel for the whole crew (`core/talk_key.h`).

Save, then restart the unit. The Status page's **Crew radio** group shows
the state (`On channel`, `No clock`, `No network` (neither the WLAN nor
Reticulum up), `No channel key`, `Self-test failed`, `No audio`,
`Paused: memory`, `Stopped`; the screen also shows a running radio whose
status has not been refreshed for 5 s as stopped), who is on the channel, the clock's
source, packet and audio counters, the free DMA RAM and, with Reticulum on,
where it stands (`Waiting for the clock`, the node and why it cannot be
reached, or its links, peers, packets each way, relayed and reconnects).

## Wire compatibility

The core mirrors crew-radio's `Packet.kt`, `Hello.kt`, `ChannelCrypto.kt`,
`Ingress.kt`, `RateLimiter.kt`, `SeqTracker.kt`, `audio/Mixer.kt` and
`audio/Conceal.kt`, rule for rule:

- 18-byte header `'P' 'T' | 4 | codec | ttl | hops | senderId | seq | time`,
  then `nonce(12) | ciphertext | tag(16)`, AES-256-GCM, the header with the
  ttl zeroed as AAD; packets over 1024 bytes dropped unread.
- Packet key: PBKDF2-HMAC-SHA256 over the channel key's UTF-8 bytes, 600 000
  rounds, salt `CrewRadio channel key v4`, written out by hand as in the app.
- Receive order: the WLAN source address's own budget (500/s, burst 1000,
  as the app's `SourceLimiter`; before the parse), global budget, our own
  id, AEAD (junk budget on failure),
  60 s replay window, seen-cache look, sender budget, seen-cache write; the
  caches (16384 audio, 2048 hello) and sequence marks for the process.
- Hello v2 with `versionCode = 0`, as the plugin sends, so no phone marks
  this node OLD BUILD; names sanitised by the shared rule (Cc and Cf
  dropped, white space collapsed).
- The relay rule (`relay_ttl`): capped at the signed budget, decremented from
  what came in, within this node's own hop limit.

`tests/vectors/vector.json` and `rns.vector.json` are copies of crew-radio's
`sk-plugin/test/` files (crew-radio commit `3ad26c4`), the same files the
app's `CrossLanguageVectorTest` and the plugin's tests read. **When the
crew-radio wire changes, copy them again and make these tests pass in the
same change**: the wire has no legacy mode, so a node on the old format is
silent to the crew.

Not ported, because the board has no use for them: Bluetooth Classic (the
P4 has no radio, the C6 only BLE), Wi-Fi Aware (not offered through
ESP-Hosted), the Aware secrets.

## The FreeRTOS design

Three tasks on core 0 (four with Reticulum), away from Arduino's loop task (core 1, where SensESP
can wait 5 s on the C6); each piece of state has one owner and the audio
path takes no lock (`src/esp/radio_runtime.*`).

| Task | Priority | Owns | Paced by |
|---|---|---|---|
| `cr-audio` | 17 | the Opus decoders (8, made once), `Playout`, `Mixer`; the `TalkKey`, the encoder, the `Transmitter` and its own send socket; writes I2S to the speaker and reads I2S from the mic | `i2s_channel_write`/`read` blocking on the DMA: the I2S clock paces it |
| `cr-net` | 16 | the receive socket and `NetNode`: receive, `Ingress`, peers, roster, the hello once a second, the bridge; publishes where this node's voice goes (`core/peer_snapshot.h`, a seqlock) | `recvfrom` with a 100 ms timeout (20 ms with Reticulum) |
| `cr-beat` | 3 | `ClockPolicy`, `MemoryGuard`, `Supervisor`, SNTP | once a second |
| `cr-rns` | 2 | the TCP connection to the Reticulum transport node and the `ReticulumNode` in it (announces, links, key proofs, `Carry`) | `select` with a 10 ms timeout; the node's tick once a second |

- `cr-net` -> `cr-audio`: a single-producer/single-consumer ring of
  admitted frames (`core/spsc_ring.h`), 32 slots; a full ring drops and
  counts. The other way, nothing: `cr-audio` reads the peer view `cr-net`
  publishes and sends with its own socket (sharing `cr-net`'s descriptor
  would race its reopen), unicast to every peer, the group and broadcast
  too for the first five frames of each burst (`BurstFloor`) or while no
  peer is known, as the app's LanTransport.
- One receive pipeline for both transports: what comes over Reticulum is
  handed to `cr-net` through a ring and goes through the same `Ingress`, so
  it and the roster need no locks, where the app has a thread per
  transport. `cr-rns` is a task of its own only because its signature work
  (an announce or a link: some tens of milliseconds) must never hold up the
  audio; it runs below `cr-net` and `cr-audio`, and below `cr-beat` too, so
  a flood of signatures cannot hold up the memory guard that sheds it.
- The socket: bound to the port, joined to the group on the station
  interface, re-opened when the address changes; hellos go to the group,
  the subnet broadcast and every peer heard (`core/lan_peers.h`). Peers are
  learned as the plugin learns them: only from packets that opened with the
  key, were a fresh first copy, came straight from their sender (hops ==
  ttl: a relayed copy arrives from the relay) and moved its sequence on (a
  late frame, replayed from elsewhere, teaches nothing). The table holds one
  address per (authenticated) sender id, so a fresh packet the unit missed,
  replayed from a spoofed address, moves only that sender's entry until its
  next hello, and pushes nobody out. Stricter than the app, which learns
  from every packet that opens.
- No resampling: the ES8311 runs at 16 kHz from MCLK = 256 fs, and libopus
  decodes at 16 kHz natively. libopus is pschatzmann/codec-opus (1.6.1,
  fixed point), pinned to one commit in both the 4"'s `platformio.ini` and
  the tests. That build keeps its scratch memory in one global buffer, so
  every libopus call, the encoder's too, stays in `cr-audio`. It is also
  built with libopus's assertions on (its `config.h`), and a failed one
  calls `abort()`, which restarts the unit: none trips at the fixed
  24 kbit/s, complexity 5, constant bitrate (3000 frames each of silence,
  tone, noise, square and DC), nor in the decoder (2 M fuzzed packets), but
  the encoder's do at 6-12 kbit/s. Do not make the bitrate a setting
  without a libopus built without them.
- Decoders as the app's: one taken over only once its talker has been
  silent 2 s (else the newcomer's frame is dropped), a gap of up to three
  frames concealed and a longer one not.
- I2S full duplex on one controller: out to the ES8311, in from the ES7210
  (MIC1 left, MIC2 right), the same stereo 16-bit slots both ways, three DMA
  buffers of one frame. The ES7210 is optional: without it the unit listens.
- Talk keys: the button's GPIO is read by `cr-audio` once a frame; the
  two-finger hold comes from the gauges' touch port (`common/src/lvgl_port`,
  which reads two points and shows LVGL a release while two are down, so a
  hold never swipes a page), passed in from the loop task every 20 ms; a
  sample older than 150 ms is a release, since the loop can wait seconds on
  the C6. The time-out's lock-out ends only when every input is let go,
  not at a moment when talking is not allowed. The button's GPIO may not be
  one the board uses, the C6's SDIO lines (14-19) and reset (54) included.
- The codec is set up once, in `setup()`, over I2C port 0 through ESP-IDF's
  legacy driver: the one ESP32_Display_Panel installs there for touch
  (linking the new driver beside it aborts at boot).
- Large tables are allocated once at start and go to PSRAM (over 4 KB):
  the seen-caches (~0.7 MB), the mixer (~110 KB), the decoders (~150 KB).
  The receive pipeline's caches outlive a task restart, so a replay does not
  get in through one.
- A new random sender id every boot: the phones remember each sender's last
  sequence number for their whole run.

## Safeguards

1. **The radio cannot take the gauge down.** `cr-beat` supervises the other
   tasks: one that stops checking in for 2 s gets them both stopped and
   started again, never the unit; after three restarts in ten minutes, or a
   task that does not stop, the radio stays off ("Stopped") and the gauge
   carries on. Radio faults are kept out of `link_watch`
   (`core/supervisor.h`).
2. **A shedding ladder for the C6 link's memory.** The gauges' worst failure
   is ESP-Hosted running out of internal DMA RAM. Below set floors the radio
   steps down, logged with the `heap` line: Reticulum traffic first (the boat
   comes before a link ashore), then the group/broadcast copies of audio
   (hellos keep theirs), then transmitting, then receiving ("radio paused:
   memory"); back up with hysteresis (`core/memory_guard.h`). The floors
   (96/64/48/32 KB) are provisional, to be measured at bring-up.
3. **A power-on known-answer test.** Before going on air the radio opens,
   parses and re-seals the packet in `vector.json`, compiled into flash; on
   a mismatch it stays off with "Self-test failed" and never transmits
   (`core/self_test.cpp`; a test checks its bytes against the file).
4. **A clock it can trust, never taken from the channel.** Every packet
   more than 60 s off is stale, so without a clock the radio neither sends
   nor plays ("No clock"). SNTP (the boat's router first, then
   pool.ntp.org) or the boat's GNSS through Signal K
   (`navigation.datetime`); a sync is trusted for a day. Signal K keeps a
   path's last value for ever and sends it again on every subscribe, so a
   GNSS time counts only when two readings advance by the real time between
   them: a dead receiver's frozen time never sets the clock
   (`core/clock_policy.h`). Adopting the time from other nodes' packets
   would let a recording set the clock back.
5. **The packet key is derived at save time** (the card's Save waits a few
   seconds, pausing so the idle task's watchdog keeps running) and kept in
   NVS: a restart is back on air at once, not after seconds of PBKDF2. The
   channel key itself is never stored.
6. **Per-sender playout depth** (phase 4): from each sender's arrival jitter
   (RFC 3550 style, by sequence number against arrival), 40-120 ms instead
   of the app's fixed 40 ms. Local only; the wire is unchanged.
7. **A two-microphone voice gate** (phase 4), if the board wires two mics:
   wind noise is uncorrelated between two close mics and speech is not, so
   a coherence gate does not open for wind.

## On the display

Mock-ups, drawn with the gauges' own LVGL code and rendered by the preview:

- **Halo** on every gauge page: nothing but a radio pip beside the status
  dot until someone talks; then an opaque band over the bezel (outside the
  dial, which ends at 0.85 R) with the talker's name along the top arc,
  green, dashed through a relay or over Reticulum; red with `TALKING` and
  `n LISTENING` while this unit transmits.
- **Crew page**, a page style: the roster as a compass, each member a pip
  with name and the phones' 1-4 bars, the talker glowing; channel name,
  head count in the LCD, one status line, volume arc.
- **Station**: the Crew page with a talk disc in the middle and the channel
  name engraved in the bezel.
- **Talk keys**: a two-finger hold anywhere (swipe and one-finger hold are
  taken) and a GPIO button on the 40-pin header; diagnostics on the web
  UI's status page, not the round screen.

## Reticulum (phase 2b)

The app's Reticulum design (`rns/`, mirrored by the plugin's `lib/rns/`),
ported rule for rule into `src/core/rns/` and checked against the same
`rns.vector.json` (made against RNS 1.5.4 itself):

- **One TCP connection** to a transport node, HDLC framing, the packet
  format without interface access codes (`rns_packet.h`). Reticulum's own
  crypto: SHA-256, HKDF, the AES-256-CBC + HMAC token, its padding removed
  only after the MAC (`rns_crypto.h`); identities, announces and their
  signatures (`rns_identity.h`); links with their request, proof, RTT,
  keep-alives and close (`rns_link.h`).
- **The curves by hand** (`curve25519.h`): mbedTLS 3.6 has no Ed25519, so
  X25519 and Ed25519 are ported from the app's `Curve25519.kt` (the
  public-domain TweetNaCl design: constant-time ladders, the constants
  checked against their definitions by the tests), against RFC 7748 and
  RFC 8032's vectors. SHA-512 is mbedTLS's.
- **The node** (`reticulum_node.h`): destination `crewradio.channel.<tag>`
  from the packet key, a fresh identity every boot; announces within rnsd's
  budget (`announce_budget.h`: on connect, and while alone every 5 min with
  two to spare); the lower destination hash dials, the other takes over
  after 5 s when a newcomer has not heard it; a link carries nothing until
  the far end's key proof (`carry.h`) checks out; announce checks and link
  requests are budgeted before their signature work, a full table evicts
  only what has not proved the key and is past its grace. The plugin's two
  dead-connection rules too (lwIP, like Node, has no `TCP_USER_TIMEOUT`):
  every confirmed link silent 12 s with nothing from the node, or an
  announce a connection that echoes them did not echo.
- **The bridge** (`net_node.h`): a first copy, or one that reaches further,
  goes on to the other transport with the app's hop rule
  (`Ingress::relay_ttl`); never back where it came from. The hello says
  Reticulum (flag 8) while connected; a member heard over Reticulum is
  drawn dashed. Questions for the boat (the app's Ask boat data frames) are
  the plugin's to answer and are dropped here.
- **Opus over Reticulum.** A PCM frame is two link packets; the phones play
  it, but Opus (the default) is one.

## Phases

| Phase | Content | Done when |
|---|---|---|
| **0. Core** (done) | The wire format, crypto, receive pipeline, mixer and concealment ported; host tests against the app's vectors; CI job | The vector packet byte for byte, under ASan/UBSan |
| **1. Listen** (written, to be run) | ES8311 + amplifier, I2S, the WLAN socket, Opus decode, hello and roster, clock (SNTP, GNSS), self-test, supervisor, memory guard; the card and status lines in the 4" firmware | Phones list the unit; the plugin's announcements are heard clearly; the `heap` line steady for an hour with two talkers |
| **2. Talk** (written, to be run) | ES7210 capture, two-finger and GPIO talk keys with the time-out, Opus or PCM, the burst floor, key beeps, half duplex | The phones' Status counters show no loss from the unit |
| **2b. Reticulum** (written, to be run) | The `rns` port (curves, crypto, packets, links, the node), `cr-rns`, the bridge, the card's Reticulum fields | A phone ashore and the unit talk through rnsd 1.5.4; a phone on board hears the phone ashore through the unit |
| **3. On the screen** (written, to be run) | The halo on every page, the "Crew radio" page style, the radio's state on the info overlay, the roster's 1-4 bars (`core/link_quality.h`), a channel name in the card | Preview renders both; 24 h with no restart caused by the radio |
| **3b. Station** | The talk disc (one-finger hold on the disc, which must not fight the page gestures), a mute on the screen, volume by dragging the arc | The station screen in use at the nav table |
| **4. Extras** | Adaptive playout, two-mic wind gate | Measured against the fixed buffer and the one-mic gate in engine noise |

## First power-up (phase 1)

Nothing of this has run on a board yet. Before trusting it, with a phone
on the channel and the serial log open (`pio device monitor`):

1. **Off by default.** A unit with the card untouched: no `radio:` lines,
   the gauge as before.
2. **Switched on, no key:** `Crew radio on, but no channel key saved`.
3. **With the key:** `ES8311 chip id 8311`, `Crew radio as "<name>"`,
   `Listening on port 47474, <ip>`; within a few seconds of WiFi, `SNTP from
   <gateway>` and the Status page's Radio clock `SNTP` (offline: `GNSS
   (Signal K)` once two `navigation.datetime` readings have come in).
4. **The phones list the unit** under its name; the unit's Radio crew line
   lists the phones.
5. **A phone talks:** clear audio, no buzz or pitch shift (I2S format or
   MCLK wrong otherwise), no pop at start; the Radio audio line counts
   frames, with few concealed.
6. **The plugin's announcement** (its web page's Say) is heard clearly.
7. **Memory:** the boot log's `heap` line and the Radio memory line with
   two phones talking for an hour; the memory guard must stay at `Nothing
   shed`. This is where the floors are set from.
8. **Multicast through the C6:** if the phones are heard only once they
   have heard the unit, ESP-Hosted is not passing multicast; the broadcast
   and unicast copies keep it working, and it is noted here.

Then talking (phase 2):

9. **The mic:** `ES7210 at 0x40, MIC1 + MIC2`; the Radio talk line's mic
   level moves when someone speaks near the unit (a level stuck at 0: the
   I2S input or the ES7210's format is wrong).
10. **Two fingers on the screen:** `Talking` in the log, the phones show the
    unit talking and hear it; the page does not swipe or change brightness.
    Lift them: `Listening`.
11. **Which mic:** try MIC1 and MIC2 in the card; keep the one that sounds
    closer and cleaner (the board's layout decides).
12. **Gain:** at 30 dB a voice at arm's length should reach the phones at
    about the level a phone sends; raise or lower it in the card.
13. **Opus:** the Radio talk line counts frames sent; the phones' Status
    shows no loss; with Opus off the same works as PCM.
14. **The time-out:** hold for 90 s: three low beeps, the mic cuts and stays
    cut until released.
15. **A button**, if fitted: the same with the GPIO set.

Then Reticulum (phase 2b), with an rnsd 1.5.4 transport node (crew-radio's
`docs/RETICULUM_HUB.md`) and a phone with its Reticulum tile on:

16. **Connect:** `radio-rns: Connected to <host>:<port>`; Radio Reticulum
    on the Status page says so. A wrong host: `can't connect (name not
    found)` and a retry that backs off to 15 s.
17. **Linked:** within seconds of the phone joining, `1 link, 1 peer`; the
    phone lists the unit with its Reticulum mark, and the unit draws the
    phone dashed.
18. **Talk both ways** with the phone off the WLAN (mobile data); the Radio
    talk and audio lines count, Reticulum `out` and `in` too.
19. **The bridge:** a second phone on the boat's WLAN only hears the phone
    ashore, and back; `relayed` counts. Relay off in the card: it does not.
20. **A dead connection:** pull the hub's network cable; within about 15 s
    the unit says `connection lost (silent)` and reconnects once it is back.
21. **Memory:** the `heap` line with the link up and someone talking; at
    the first floor the memory guard says `Reticulum off` and the
    connection closes.
22. **Timing:** a link comes up in a second or two (the signature work on
    the P4); the log shows no audio underruns while a link is set up.

## Settings and the key

The channel key is typed into the card, never put in `secrets.h` or the
repository, and never stored: only the packet key derived from it (NVS) and
its last four characters (to show which one). Without flash encryption
anyone holding the unit can read the packet key out of NVS; flash
encryption fixes that but cannot be undone on the board, so it is
documented, not the default.

## Tests

```bash
cmake -S p4-gauge-crew-radio/tests -B build/crew-radio-tests && cmake --build build/crew-radio-tests -j && build/crew-radio-tests/crew_radio_tests
```

| File | Covers |
|---|---|
| `test_vectors.cpp` | The app's vector: key derivation (ASCII and non-ASCII key), the hello, the sealed packet byte for byte; the Reticulum tag and confirm key |
| `test_packet.cpp` | Header round trip, malformed packets, clamping, the AAD, the replay window and its wrap, the relay ttl |
| `test_hello.cpp` | Hello round trip, name truncation on a code point, the wire contract, strict UTF-8, the name rule |
| `test_crypto.cpp` | Seal/open, tamper and wrong-key refusal, PBKDF2 against Python's hashlib |
| `test_rate_limiter.cpp` | The three budgets, the bounded and swept sender table |
| `test_seen_cache.cpp` | Lookup, insertion-order eviction, and 200 000 random operations against a reference model |
| `test_seq_tracker.cpp` | Gaps, late frames, the wrap, least-recently-heard eviction |
| `test_ingress.cpp` | The pipeline's order: budgets before and after the AEAD, stale before the cache, copies free, the lowered-ttl replay |
| `test_mixer.cpp` | Concealment, prefill, holes, dry queues (and dry slots that stood in for frames a later gap reports, not reserved twice), the cap, gain and cues, headroom and clipping, the sweep |
| `test_lan_peers.cpp` | Unicast once a peer is known, the burst floor, hellos' group copies, expiry, the bounded table; one address per sender (a replay moves only its own entry), one copy per address |
| `test_roster.cpp` | Names from hellos, sorted; talking; dropped after 4 s; bounded; the 1-4 bars (`core/link_quality.h`, the app's rule: missing hellos of the last ten, lost audio of the last five seconds of talk, the worse; an overdue hello counts as missing) |
| `test_clock_policy.cpp` | Trust for a day; GNSS only when it advances with real time (a retained or frozen value never sets the clock); SNTP preferred; Signal K's datetime parsed, impossible dates refused |
| `test_source_limiter.cpp` | One budget per WLAN source address: a burst then its rate, each address its own, the least recently used evicted and never a newcomer refused, a clock that steps back |
| `test_supervisor.cpp` | Stall detection across the 32-bit wrap, three restarts then off; the memory ladder with hysteresis |
| `test_self_test.cpp` | The power-on test passes and its bytes are the vector file's |
| `test_spsc_ring.cpp` | Capacity and order; 200 000 frames across two threads, none lost or reordered |
| `test_net_node.cpp` | Two nodes with one key: hello onto the roster, audio handed on once with its gap; a stranger teaches nothing; a stale packet plays nothing and teaches no address, nor does a replayed copy; our own packets dropped unopened; a keyless flood from one host does not silence the crew; neither a relayed first copy nor a late frame moves its sender's address; a hello that does not decode makes no member; an audio-only talker keeps its bars; the bridge (a first copy to the other transport with one hop spent, never back, the hop rule, off unless asked; a hello over Reticulum marks its member) |
| `test_playout.cpp` | The decoder pool (own decoder per sender, the quietest taken over only once it has paused, a ninth talker never resetting a live one, idle released); PCM and Opus to the mixer with short gaps concealed and a long one not |
| `test_radio_state.cpp` | What the radio reports while it runs: Stopped whenever a task is not running, whatever the clock or the memory guard say; then Paused, No clock, No network, On channel |
| `test_radio_settings.cpp` | The app's channel key rule, the multicast group and port, the key mask, the talk button's GPIO (the board's own pins and the C6's SDIO refused), the transport node's `host[:port]` |
| `test_talk.cpp` | The talk key's debounce and its 90 s time-out with the lock-out until release, kept through a moment when talking is not allowed; the transmitter's packets open on another node as this node's voice, in sequence; the burst floor; the peer snapshot never read torn (two threads); the key tones; the mic channel, level and ES7210 gain codes |
| `test_curve25519.cpp` | X25519 against RFC 7748, Ed25519 against RFC 8032; a tampered message or signature, another key, S + L and a point off the curve refused; random keys sign, verify and agree; the constants against their definitions |
| `test_rns_primitives.cpp` | HMAC (RFC 4231, a refusal leaves no MAC), HKDF (RFC 5869), the token (tamper, wrong key, short), PKCS#7, packets and the hash that ignores the transport, HDLC framing with junk and oversized frames, announces (tampered, foreign, with a ratchet), links (an impostor's proof, data, keep-alive, close, MTU signalling, a low-order key), `Carry` whole and in parts with loss, rnsd's announce budget |
| `test_rns_vectors.cpp` | `rns.vector.json`: the name hash, identity and destination; the announce byte for byte, framed and parsed; the link request, id, proof, key, token, data packet and RTT; each end's key proof; the PCM-sized cut |
| `test_rns_node.cpp` | The app's ReticulumNodeTest on a fake medium: two nodes link, prove the key and carry the channel (PCM in two parts); a stranger with another key gets nothing, its reflected proof included; a lost proof is resent; another channel and an older announce; silence, redials, time-outs; full peer and link tables; flood budgets; a link's own rate budget; announces on connect and while alone; the takeover; a dropped connection; and the plugin's two dead-connection rules |
| `test_opus.cpp` | libopus decodes the app's 20 ms frames at 16 kHz and refuses anything else; the encoder round-trips through it (with `CREW_RADIO_OPUS`, on by default) |

mbedTLS 3.6.4 (the line ESP-IDF 5.5 ships) is downloaded by the build,
pinned by its SHA-256; ArduinoJson reads the vectors; libopus is the
firmware's codec-opus commit. `-DCREW_RADIO_OPUS=OFF` builds without libopus
(and without its three tests). `-Wall -Wextra -Werror`, AddressSanitizer and
UBSan, as the gauge tests (MinGW: `-DSANITIZE=OFF`). CI runs them on every
push to `main` and every pull request.
