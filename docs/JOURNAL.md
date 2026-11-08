# Work Journal

## 2026-10-29 — First firmware for the four gauges

Two PlatformIO projects (Waveshare ESP32-P4-WIFI6-Touch-LCD-4C and
ESP32-S3-Touch-LCD-2.1) sharing `common/`: classic steel radial gauges, a
rotating compass card and a sea-horizon heel/trim page, drawn with LVGL 9.3,
fed by SensESP. One env per unit (port/starboard, OTA and USB), with the OTA
password and addresses in the gitignored `include/secrets.h` as in
drive-remote-controller.

Board bring-up goes through ESP32_Display_Panel: its built-in board file for
the 2.1", and a custom board file for the 4" built from Waveshare's BSP; its
JD9365 init table matches Waveshare's Arduino example byte for byte.

Verified at the time: all eight envs compiled locally (then CI took over one
env per board); every page rendered on the host at 480 and 720 px
(`tools/preview`). Not verified: anything on hardware -- no unit has
been flashed yet.

CodeRabbit's first review led to: an empty OTA password in the committed
example, `$`/whitespace handling in `ota_auth.py`, restart on display-init
failure, reboot on LVGL asserts, a bounded idle delay in the loop, and
preview-tool robustness. CI added (`.github/workflows/ci.yml`): one build per board
(`p4_stbd`, `s3_stbd` -- the port envs differ only in `-D` flags), the host preview, and the credential checks.

## 2026-10-29 — Multi-agent review round 1

Seven parallel reviews (LVGL drawing, SensESP data path, board bring-up
against Waveshare's BSP/Arduino sources, numerics, touch/page manager,
CI/security, docs). Each finding was checked against the code before fixing.
The ones that changed behaviour:

- **Stale data shown as live** (non-negotiables 1-2): signalk-server replays
  its last value on every (re)connect and SensESP drops the timestamp, so a
  dead source looked live for 10 s. The first sample per path after each
  connection state change is now ignored. A `null` delta now blanks the gauge
  at once; a periodic sweep clears stale values (no millis() wrap revival);
  finiteness is checked after unit conversion; a stale compass card or
  horizon is dimmed instead of looking like a reading; LCD overflow shows
  a row of `E`s, distinct from no-data dashes.
- **Recover, don't freeze** (non-negotiable 3): loop task watchdog (off during
  OTA uploads); a display flush that never completes restarts the unit; the
  4" alternates the GT911 address (0x5D/0x14) across restarts instead of
  boot-looping dark on a 0x14 board.
- **Security**: OTA password restricted to shell-safe characters (it was
  passed unquoted to the shell); optional web UI admin login and AP password
  from secrets.h, AP off when the boat WiFi is configured; every `secrets.h`
  in the tree gitignored and checked by CI.
- **Touch**: a swipe can no longer also fire a long press (brightness) or a
  tap; NVS writes debounced and skipped when unchanged.
- **Resources**: one LVGL buffer instead of two (the flush blocks anyway);
  draw tasks culled to the render strip (compass ~4x fewer); 2.1" touch read
  on its interrupt; USB-CDC writes never block on the 4"; both 4" app slots
  now end at or below 16 MB.

Verified: both boards build without warnings from this code; the preview
renders every page; stale/overflow states rendered and inspected; incremental
redraws are pixel-identical to full redraws on every page; the CI credential
check passes the clean tree and fails each injected credential.

## 2026-10-29 — Sea horizon: level horizon, moving boat

At the owner's request the attitude page now keeps the sea horizon fixed and
level and moves a boat symbol (seen from astern: hull, deck, mast) instead:
it heels with `roll` (starboard down turns it clockwise) and rises with
`pitch` (bow up), against a fixed trim ladder at 5 and 10 degrees. The sky,
sea and ladder are now part of the static face, so only the boat and the heel
pointer redraw. Rendering it exposed a latent bug: `draw_text()` queued a
pointer to the caller's text; it now sets `text_local` so LVGL copies it.

## 2026-10-29 — Multi-agent review round 2

Three reviews (data path and SensESP internals, app/security, UI) of the
round-1 fixes. What changed:

- **Stale data**: Signal K replays every source's cached value on
  subscribe, so the first second after each (re)connection is dropped
  (`client_generation()` window) and nothing is stored while the client is
  down. A `null` blanks a gauge only if no valid sample came within 2 s, so
  one source losing data cannot blank a value another source still sends.
- **The stale dimming never worked**: a style opacity reaches only the
  descriptors LVGL initialises itself, not a draw callback's. The compass
  card, the boat symbol and the heel pointer now copy `layer->opa` into
  their descriptors; the preview renders every page stale as well
  (`out/stale<size>_*`) and CI publishes a stale contact sheet.
- **Security**: the web UI login from secrets.h was ignored (SensESP builds
  its authenticator once, from saved config) -- it is now written to the
  saved config with a restart when it differs. AP password must be empty or
  WPA2-length; admin user required with a password (`static_assert`s).
  `ota_auth.py` uses an allowlist (also safe on Windows shells).
- **Recover**: touch start-up moved to `common/src/board_start.cpp`: last
  good GT911 address (NVS) first, then the other, then run without touch;
  the loop watchdog is off while a factory reset formats the filesystem and
  during an OTA upload, which now shows a notice instead of a frozen gauge.
- **Small**: LCD with no room draws nothing; the status dot's dark test uses
  the quantised colour; a gesture with no direction is ignored; the
  misleading `board_upload.maximum_size` lines are gone (the partition table
  sets the app size).

Verified: both boards build (S3 2.80 MB of a 6.25 MB slot, P4 2.91 of
8.3 MB); live and stale renders inspected; incremental redraws are pixel-
identical to full redraws; ASan/UBSan clean on the host.

## 2026-10-29 — OTA log hygiene, CI hardening (review round 3, part 1)

CodeRabbit found that the espressif32 builder runs espota with `--debug`,
which logs the `--auth` password: `scripts/ota_quiet.py` (post: script)
drops the flag (checked with `-t envdump`). The empty-password check now
requires each key once, as exactly one `""`. From the round-3 CI/docs
review: `platformio.ini` may carry no `upload_flags`, `--auth` or
`SECRET_*PASSWORD`; the compiled-object cache restarts weekly instead of
growing for ever; the contact sheet fails on a missing or blank render;
README, the build-flash skill and secrets.example.h now describe the null
grace, the 1 s replay window, the AP rules and the touch retry as coded.
Kept: LVGL fetched by release tag (a SHA would force a full clone).

## 2026-10-29 — Multi-agent review round 3 (deeper)

Eight reviewers, each on a narrow scope and required to trace failures
against the fetched library sources (SensESP 49fdb8a, LVGL 9.3.0,
ESP32_Display_Panel 1.0.4, Arduino-ESP32 3.3.11, signalk-server, the
Signal K spec and n2k-signalk). What changed:

- **Replay guard (must-fix, found by two reviewers)**: SensESP's client
  generation only changes when it tears down an old client, so on the first
  connection after every boot the server's cached replay was taken as live;
  the WebSocket library's own reconnects kept the generation too; and the
  window compare went negative after 24.8 days of one connection, dropping
  everything. Now: a connection epoch bumped on every connection state
  change (the disconnect reaches the loop long before the library's own
  reconnect; "connected" and the replayed deltas are polled in no fixed
  order), a per-slot "not yet seen" flag, and a `replaying` flag that the
  1 s sweep also clears, so the time compare only runs inside the window.
  A verification pass found the first version of this fix relied on that
  ordering and left a quiet path's window open; both corrected.
- **Null grace**: a lone `null` after a recent value is now remembered and
  blanks the gauge ~2-3 s later unless a valid value follows.
- **Web UI login (must-fix)**: with no saved HTTP settings (every fresh
  unit) `load()` left memory untouched, so the login never applied. The saved
  state is now read from defaults + file, and the save is read back before
  restarting, so a failed write cannot boot-loop.
- **Touch fallback (must-fix)**: `Board(const BoardConfig&)` goes through
  driver factories that are empty in this build, so "run without touch"
  could never start the display. It now uses the default-config board and
  drops its touch entry; the attempt counter advances before each try.
- **OTA**: a failed authentication no longer reboots the gauge (only an
  upload that started does); `ota_auth.py` treats an IP/.local port as OTA
  whatever the protocol, and refuses a password defined twice or followed by
  anything but a comment.
- **Touch**: a slow drag no longer fires the long press (brightness): a hold
  must stay within 8 % of the radius; long press 800 ms; a failed touch read
  keeps the previous state instead of a release.
- **Data**: heel-only attitude sources work (trim dashes); the coolant
  default is `propulsion.<side>.temperature`, which NMEA 2000 gateways
  publish; the heading LCD shows `°M` for a magnetic path; absurd headings
  are refused; `wrap360` can no longer return 360; an empty path is refused
  in the web UI; pages and subscriptions agree when a side has no pages.
- **UI**: the stale boat symbol dims as one layer (no darker seams at
  overlaps); the hull's shared triangle edge no longer shows a seam; the
  boat's drawn roll is clamped to +-90.
- **Credentials**: `secrets.h*` backups ignored and CI-checked (any case),
  and a filled-in `SECRET_*PASSWORD` in any C/C++ file fails CI.
- **Docs**: AP settings only seed a unit; erase + USB flash recovers an
  unreachable unit; the 4" has two USB-C ports. Correction to round 1: the
  4"'s `Serial` writes do not block, but ESP-IDF's console output may.

Declined: brightness ping-pong instead of the 100/60/30/10 cycle (the cycle
is predictable, and the 800 ms hold makes accidental presses rarer);
LVGL by SHA; enforcing AP settings after first boot (documented instead).

Verified: both boards build; live and stale renders inspected (boat crops
at 720 px); incremental redraws pixel-identical to full; a heel-only,
then garbage-roll attitude run is clean under ASan/UBSan; every CI
credential rule tested against injected failures.

## 2026-10-30 — 4" DSI bus on v3 P4 silicon

ESP32_Display_Panel 1.0.4 fills a partial DSI host config with
`MIPI_DSI_PHY_CLK_SRC_DEFAULT`, which ESP-IDF 5.5 keeps as the pre-v3
`PLL_F20M`; v3 silicon has no such PHY PLL reference, so
`esp_lcd_new_dsi_bus()` aborts. `start_board()` now replaces the partial host
config with a full one using `MIPI_DSI_PHY_PLLREF_CLK_SRC_DEFAULT` (only when
`CONFIG_ESP_REV_MIN_FULL >= 300`). Not verified on hardware.

## 2026-10-30 — Touch late after boot; responsiveness

Reported on hardware: the gauge appears at boot, but touch takes a long time
to react. Cause (from the code; to be confirmed with the new boot log):
the gauge is drawn before SensESP starts, and touch only answers once
`setup()` returns. Inside `get_app()` SensESP mounts SPIFFS and loads every
setting; a setting never saved costs two or three `exists()` misses, each an
`fopen` plus a `stat` that walk the lookup page of every SPIFFS block. With
a ~16 MB filesystem (4") that is ~4000 flash reads per walk, dozens of walks
per boot; 3.4 MB on the 2.1".

- Filesystem cut to 1 MB on both boards (`partitions_32MB.csv`; new
  `partitions_16MB.csv` for the 2.1"). Takes effect with a USB flash and
  resets SensESP's settings once (README "Boot time").
- Pages are kept once built; the others are built in the background (one
  per 250 ms tick, from 5 s after boot, only after 1.5 s without a touch,
  only with PSRAM to spare, else back to one page at a time), so a swipe
  only unhides a page. Host: a page shown again after switching is
  pixel-identical to a fresh build; ASan/UBSan clean.
- Boot milestones (`boot:`) and loop stalls >= 150 ms (`loop:`, SensESP or
  LVGL side) are logged, to confirm the cause on the units.

Measured on the host (x86): building a page takes 2-6 ms; expect tens to
hundreds of ms on the boards -- the `Page ... built in` log line says.

## 2026-10-30 — Zones from the Signal K server; SKip-like gauge settings

Asked for: zones from Signal K instead of the fixed red bands, and gauges
configurable the way SKip configures its steel gauge widget.

- Dials now draw the path's `meta.zones` from the server (SensESP subscribes
  with `sendMeta=all`; `GaugeListener` opts in to metadata), converted to the
  display unit and clipped to the dial, in SKip's dark-theme colours
  (nominal/alert/warn/alarm/emergency; `normal` not drawn). Without zones on
  the server a dial shows none. New metadata rebuilds the page.
- Each page's web UI card (`GaugeSettings`, `/Gauges/<id>`): show/hide,
  label, path, display unit (`units.cpp`), scale by hand or the server's
  `displayScale`, show zones, decimals, data timeout. Validated before
  saving; an old `{"sk_path"}` save still loads. Takes effect on restart.
- `gauge_resolve.cpp` (host-safe) turns table defaults + settings + metadata
  into the `GaugeDef` a page is drawn from, recomputing ticks for a new range.
  Two spare pages (Depth, Battery) start hidden.

Verified: ASan/UBSan host test of the resolver (rpm, °C/°F, compass °M,
attitude bands); both boards build; preview rendered with sample server
zones and looked at. Not verified: on hardware, and against a live server's
metadata.

## 2026-10-30 — Generic device docs

The README, AGENTS.md, board READMEs, the build-flash skill and the
platformio.ini comments now name devices only by size and side (4" / 2.1",
starboard / port): no unit numbers and no boat-specific IP addresses.
`secrets.example.h` defaults each device's OTA address to its mDNS name
(`gauge-4c-stbd.local` ...) and leaves the static-IP gateway and DNS empty;
a real `secrets.h` sets IPs where needed. Both boards build from the
template; credential checks pass.

## 2026-10-30 — User-defined pages

Asked for: the number of pages, and each page's title, path, range and
zones, user-defined like SKip, instead of a fixed set of gauges plus spares.

- The fixed page table is gone. The web UI's "Pages" card sets how many
  pages to swipe through (1-12, `/Pages/count`, default 6); each has a card
  ("Page 1".."Page n", `/Pages/<n>`) with one of three styles -- Dial,
  Compass, Sea horizon -- and title, path, display unit, scale (by hand /
  server displayScale), zones on/off, decimals and data timeout.
- The side (env) only seeds the factory pages: its engine's rpm, speed,
  heading, coolant, oil, sea horizon; pages 7-12 start as dials with no
  path (not subscribed, dashes). A page switched
  to another style starts from that style's template (a dial on 0..100 in
  the new "SI" unit: the value as sent, labelled with the server's
  meta.units).
- Preview renders the factory pages plus two example user dials (depth with
  a server displayScale, battery in SI/V with a hand-set scale).

Verified: both boards build; the schema assembles to valid JSON; preview
rendered and looked at. Not verified: the web UI cards on a device.

Follow-up the same day: the user picks how many pages to swipe through.
The "Off" style is gone; the "Pages" card (`/Pages/count`, 1-12, default =
the six factory pages) decides, and only those pages' cards are shown and
loaded at boot. Pages past the factory ones start as dials with no path
(not subscribed, dashes). `docs/PAGES.md` is the end-user guide to adding,
removing, changing and reordering pages. Both boards build; preview
rendered.

Review fix (CodeRabbit): a page no longer falls back on its slot's factory
definition. Every page starts from its style's template and takes its unit
and range from its card (Scale minimum / maximum are the dial's range unless
the server sends displayScale or the scale is set by hand); ticks are
recomputed from the range (the same as the factory ticks for every factory
dial). So settings copied between cards give the same page, a cleared dial
path stays empty, and a style change resets to the style's defaults. A unit
changed on its own converts the range (`convert_range()`, host-tested:
0-120 °C -> 0-250 °F, 0-3000 rpm -> 0-50 Hz).

## 2026-10-31 — Tests, card actions, bring-up guide

Asked for: every area of the self-assessment raised to 9.

- The logic that needs no board moved into host-safe units, used by the
  firmware and unit-tested: `value_gate.*` (replay window after a
  reconnect, `null` grace, staleness, `millis()` wrap -- from `sk_data.cpp`),
  `sk_parse.h` (only a JSON number is a reading -- from `sk_listeners.h`),
  `page_rules.*` (what a page card holds, the save rules, the card's schema
  -- from `gauge_settings.cpp`). `tests/`: 36 tests over these and the
  resolver, units and factory pages, with ASan/UBSan and -Werror, against the
  firmware's ArduinoJson 7.4.3; a mutation check (replay guard and the
  number check broken on purpose) fails 26 checks. New CI job
  "Unit tests (host)".
- Page cards: **Reset to factory settings** and **Swap with page** (one-shot
  actions; swap is the reordering), and a card shows only its style's
  fields (the schema is built on every request).
- Boot log: a hardware summary (chip, silicon revision, flash, PSRAM), the
  page count and filesystem use, and which touch address answered.
  `docs/BRINGUP.md`: first power-up step by step, with the log to expect.

Verified: both boards build; tests pass; preview rendered. Not verified: on
hardware (next: flashing through the user's computer).

Review fixes (independent review): SensESP's web UI keeps a card's form
until the page is reloaded and sends all of it with every save, so a second
save after a swap, reset or style change could undo it (a page could be
lost). Each card now carries a hidden revision (`_rev`, random at boot),
moved on by a reset, a swap (both cards) or a save the rules adjusted; a
save from an older form is refused (`page_save_action()`, tested). A plain
save leaves it, so a card can be saved twice without a reload. Also: the
touch log line prints the address that answered (was 0x00 for the board
file's default), PSRAM is the chip size rounded to the MB. A page card's
file is replaced all or nothing (`card_store.*`, host-tested; CodeRabbit):
SensESP's `FileSystemSaveable::save()` removes the old file first and reports
success whatever the write did. Now the new contents go to a staging file
(`/pgnew<n>`) and are read back, the old file moves to a backup (`/pgold<n>`,
empty if there was none), the staging file moves into place, and the backups
go. A marker file (`/pgtx`) is written before any old file moves and removed
only once every new file is in place (or every card is back): while it is
there, the boot's `recover_cards()` puts every card back to its backup, so a
power cut, or a rollback that itself failed, is finished at the next boot
(and further saves are refused until then). If the boot cannot put them
back either, the pages use their factory settings rather than a half-made
save, and the log says so. A web UI save is written
by `from_json()` itself, so a failed write refuses the save (and puts the
card back); a swap writes both cards in one replace. The tests fail every
step and every pair of steps (reads included, so a rollback fails too), and
cut the power at every step, with and without a failure before it; a swap
always ends both old or both new after the boot.

## 2026-10-31 — First hardware: the 4" board

The first board on a real device: a 4" (ESP32-P4 rev 3.2, GT911 touch at
0x14), flashed over USB from the user's PC through its CH343 UART (the boot
log comes out there too), on the boat's WiFi with a real Signal K server.

Display, touch, the boot log, WiFi (DHCP), the Signal K access request and
the subscription all worked first time. Then the unit restarted by its loop
watchdog a few seconds after every Signal K connection. Found from the core
dumps (read back from the coredump partition, decoded with the P4 gdb):

- The loop was waiting on an ESP-Hosted RPC to the C6 (SensESP's status
  page asks `WiFi.SSID()` and `RSSI()` every 5 s, on the loop) that was
  never answered. The RPC timeout is 5 s, the watchdog's too.
- The C6 ran factory ESP-Hosted firmware that does not report its version
  (`0.0.0`); `tools/c6-update` (new: the core's `ESP_HostedOTA`, over WiFi)
  brought it to 2.12.11, `Versions Match!`. The restarts stayed.
- The cause: internal DMA-capable RAM. The 720 x 72 LVGL draw buffer
  (101 KB) was in internal RAM (326 KB free when it is taken, before WiFi);
  once WiFi was up 9 KB was left, and the Signal K connection took the rest.
  ESP-Hosted could no longer allocate its SDIO buffers, the link stalled for
  good (every RPC unanswered, no traffic) and the watchdog fired. The 4"
  now puts the draw buffer in PSRAM (`lvgl_port_init(..., true)`): 98 KB of
  DMA RAM stays free with Signal K data flowing, no RPC lost, no restart.
  A "heap" line once a minute logs it.
- A path without a unit in its metadata (`navigation.attitude`) was read as
  the unit "null": SensESP's `SKMetaView::units` uses `as<String>()`. Its
  zones and scale were dropped as a unit mismatch, and an SI dial would have
  been labelled "null". The unit is now read from the raw metadata
  (`sk_meta_units()`, tested).

Also: a shared PlatformIO build cache (`build_cache_dir`, `.pio-build-cache/`
at the root, as in CI). A per-file setting is passed in code, not as a
global `-D` flag: a new flag changes every file's command line, so nothing
comes from the cache. `tests/` builds without sanitizers on MinGW
(`-DSANITIZE=OFF`). `docs/BRINGUP.md`: the C6 check and the log lines that
are expected (SensESP's status LED pin, `NO_AP_FOUND` for one of several
networks).

Verified on the 4": boot, touch address, WiFi, Signal K connection and
metadata, 4 minutes connected with flat heap. Not yet: readings against
another instrument, stale/recovery behaviour, OTA, the 2.1".

Review fixes (CodeRabbit, on the 4" bring-up commit): `tools/c6-update` no
longer uses the core's `updateEspHostedSlave()`, which skips the server's
certificate check and writes whatever it is served to the C6. It downloads
the image into PSRAM and checks it against a pinned size and SHA-256 (2.12.11:
1226800 bytes, `5b05e153...d709cb`, the same from espressif.github.io and
raw.githubusercontent.com) before writing it; a version with no pinned image
is refused. A `c6_update_pre_v3` env covers pre-v3 P4 silicon. The SPIFFS
`read()` behind the card saves fails on a short read (an early stop read as
an empty backup would have meant "no file"). `gauge_app.cpp` includes
`esp_heap_caps.h` itself.

## 2026-11-01 — Live data, the 2.1", the dial look from the boat

Both boards on the boat's network with a real Signal K server.

- **No page showed a value.** SensESP (main) gives value deltas only to
  listeners whose `wants_meta()` is false; the gauge listeners wanted both
  and so got only metadata. Each page now has a `SKMetadataListener` next to
  its value listener (SensESP merges them into one subscription entry). The
  boot log says `pageN: first reading ...` once per page.
- This boat's engines are `propulsion.stb_engine` / `prt_engine`, coolant is
  `coolantTemperature`: the engine pages were set to them in each device's
  page cards (a boat setting, not a factory default).
- **The 2.1"** (ESP32-S3 rev 0.2, CST820 touch at 0x15, on its native USB):
  first flash, boot, Signal K, all six pages live with the server's zones;
  50 KB DMA RAM free with WiFi up (draw buffer 37 KB in internal RAM).
- **Dial look**, from the user on the real screens: scale numbers closer to
  the ticks (0.625 R); from 1000 on numbered in hundreds with the unit
  "rpm x100" (`label_divisor()`, tested; thousands from 10000); the LCD is a
  plain number on the grey-green panel instead of seven segments, in
  fixed-width digit cells so a changing value does not shift, scaled above
  the largest font on the 4"; the degree sign sits with the digits (heel,
  trim, compass "291° T").
- **2.1" flicker**: constant flicker with noise on the real panel (its
  frame buffer is read from PSRAM all the time, shared with LVGL and WiFi).
  `start_board()` now sets the RGB pixel clock to 12 MHz (44 Hz) and the
  bounce buffer to 20 lines instead of the board file's 16 MHz and 10; the
  user confirmed it gone. DMA RAM left with WiFi up: 27 KB (was 50).
- **Touch lost after an interrupted boot**: `start_board()` counted each
  attempt before trying it, so a reset during the start (here: flashing,
  then a reset to read the log) counted as a failed start, and the 2.1"
  (one touch option) came up without touch. Now only a crash, a watchdog or
  our own restart moves on to the next option.
- **Info box on a swipe down** (swipe up or tap closes it): a tap opened it
  whenever a short sideways swipe was read as one.

Not yet: readings against another instrument, stale/recovery on hardware,
OTA. Both devices first try a 5 GHz-only network from `secrets.h` at every boot
(about 20 s): neither chip has 5 GHz.

Later the same day:

- **2.1" did not reconnect after a Signal K server restart** (the 4" did):
  `connect worker spawn failed` -- SensESP starts an 8 KB task for each
  connection attempt, and with the 20-line bounce buffer the largest free
  block of internal RAM was 7 KB (41 KB free in all). Back to a 10-line
  bounce buffer the reconnect worked, but the picture drifted up and
  flickered. Kept at 20 lines; the LVGL draw buffer went from 40 lines to
  20 (37 KB -> 18 KB) instead: 45 KB free, largest block 27 KB, no flicker.
- Both boards' draw-buffer size moved from a `-D LVGL_BUFFER_LINES` flag
  into `src/main.cpp` (`kBufferLines`): only `main.cpp` used it, but as a
  flag any change recompiled every file (one full rebuild per board to move
  it).

## 2026-11-01 — The 2.1" compass, and why its picture shifts

Both boards ran ~6 h after the reflash with no restart and ~12 Signal K
deltas a second each. The 2.1" logged "LVGL held the loop" (150-175 ms)
5-10 times a minute on the compass page: each step of the turning card
redrew 180 graduations and 16 rotated labels.

- **Tried and dropped: the card turned by table lookup.** The card drawn
  once (at 2x, averaged down), resampled into a polar strip, each angle
  a lookup per pixel. It looked the same as the drawn card and built in
  slices, but every step read and wrote ~1 MB of PSRAM in a burst, and the
  2.1"'s panel (fed from PSRAM) flickered and shifted up. Lesson for the
  2.1": no memory-heavy per-frame work, however fast.
- **Tried and dropped: LVGL and the pages compiled at -O2** (the framework
  builds everything -Os). Larger code means more flash fetches on the
  S3's bus shared by flash and PSRAM; the picture shifted even at rest.
- **Kept, CPU only:** the card is redrawn as the ring its marks sweep
  (`invalidate_ring()`, up to 16 bands) instead of its bounding square; it
  ignores heading changes under 0.3 degrees (the LCD shows the heading as
  it is), so at the dock it does not redraw at all; the 180 graduations are
  placed by turning one direction by 2 degrees at a time, not with a sine
  and cosine each, per screen strip. Measured with the card stepping all
  the time (course over ground at the dock): 137 ms a step (180 marks
  ~32 ms, 16 labels ~42 ms, the rest ~62 ms); at rest the slowest pass is
  16-21 ms. The preview renders the compass pixel for pixel as before.
- **Once a minute, `loop: Slowest pass in the last minute`** next to the
  heap line: how close to a stall the gauge runs, not only when it
  crosses 150 ms.
- **`preview <size> bench`** times one redraw of each page (median), in
  strips as tall as the boards' draw buffers.
- **The 2.1" picture shift, explained.** The panel is fed by an interrupt
  that copies the PSRAM frame buffer into two 20-line bounce buffers. One
  missed copy leaves the picture one bounce buffer (20 lines) out of step,
  and it stays so (ESP-IDF's restart at every VSYNC, on in this build, does
  not catch it); late copies are the horizontal lines. A copy is missed
  when PSRAM cannot be read in time: flash writes (WiFi connect, a file or
  preference saved) and heavy PSRAM traffic. Without bounce buffers (the
  panel's DMA reads PSRAM itself) the shift cannot stay, but single frames
  then jumped sideways several times a second: worse. Back at 20 lines.
  ESP-IDF's documented setup for an RGB panel in PSRAM next to WiFi (code
  and read-only data from PSRAM, an IRAM-safe RGB interrupt, a larger data
  cache) needs a rebuilt framework (`custom_sdkconfig`): to try next.

## 2026-11-01 — A 4" watchdog reboot; what shifts the 2.1" picture

- **The 4" rebooted by itself** after about 6 h (reset reason: task
  watchdog). Its core dump (read with esptool from the `coredump`
  partition, opened in `riscv32-esp-elf-gdb` with the build's
  `firmware.elf`): the loop was in SensESP's status-page timer, in
  `WiFi.SSID()`, waiting in ESP-Hosted's `wait_for_sync_response` for the
  C6 to answer `wifi_sta_get_ap_info`. The same place as on the first
  night, but this time with 78 KB of internal RAM free and the C6 on
  2.12.11: one lost reply, not a dead link. ESP-Hosted waits 5 s for a
  reply (`DEFAULT_RPC_RSP_TIMEOUT`) and the loop watchdog was 5 s
  (`CONFIG_ESP_TASK_WDT_TIMEOUT_S`), so the watchdog fired as the call was
  about to give up.
- **Fix (both boards, shared code):** the loop watchdog is reconfigured to
  12 s at start (`kLoopWatchdogMs`, `esp_task_wdt_reconfigure()`, keeping
  core 0's idle task watched and the panic with a core dump). A lost reply
  now freezes the picture for 5 s instead of rebooting. A link that never
  answers again would leave the loop running but stalling 5 s at a time,
  which no watchdog sees; so three SensESP passes of 4 s or more within a
  minute log why and restart (`gauge_app_loop()`, not during an OTA
  upload).
- **What shifts the 2.1" picture, found by trying it:** the 20-line shift
  came back at rest, on the stock build, several times. With the unit
  freshly restarted and steady, reading the page settings over the web API
  every 2 s (`GET /api/config/Pages/<n>`, files on SPIFFS) shifted it
  within half a minute (the user watching the screen). On the S3 a flash
  read or write takes the shared bus away from PSRAM and holds off the
  RGB interrupt (not IRAM-safe in the stock build) for longer than a
  bounce buffer lasts. Whatever touches the filesystem can do it: the web
  UI's configuration page, a saved setting, WiFi saving its settings.
- **Fix (2.1" only):** `custom_sdkconfig` in its
  `platformio.ini` rebuilds the framework with
  `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` (code and read-only data run from PSRAM,
  so flash access no longer stops PSRAM) and
  `CONFIG_LCD_RGB_ISR_IRAM_SAFE=y` (the RGB interrupt runs during flash
  access), as ESP-IDF's RGB LCD guide describes for a PSRAM frame buffer.
  The first attempt failed after 3.5 min with `FileNotFoundError` in
  `run_cmake`: the build was started from Git Bash, where ESP-IDF's
  `idf_tools.py` refuses to run ("MSys/Mingw is not supported"), so CMake
  and Ninja were never installed and PlatformIO only logged it. Installed
  from PowerShell, and the build runs from PowerShell: 63 min the first
  time. Result on the 2.1": PSRAM free 4878 -> 3459 KB (the code in PSRAM),
  DMA RAM 46 KB free (largest block 28 KB), all pages live. The same test
  as before, longer (60 s of reads every 2 s, a save, 20 s more reads),
  with the user watching: the picture stayed in place, no lines. CI's
  firmware job gets 90 min instead of 30 for this build.
- **Why some builds were slow:** the build cache is keyed on the compile
  command, which includes the env's own library folder
  (`.pio/libdeps/<env>`). Building `p4_stbd` after only `p4_stbd_usb` had
  been built compiled all 778 files (21 min). Build and flash with the env
  already built.
- **The shift came back at rest**, a few minutes after that test, with
  nothing touching the flash. ESP-IDF's RGB driver source (downloaded for
  the rebuild) explains it and the user's "always the same height": the
  bounce-buffer refill picks its buffer by a running count
  (`bb_eof_count % 2`); one late "buffer sent" interrupt (two merged into
  one) puts the count one behind, and every later refill goes into the
  other buffer, 20 lines out of step. The VSYNC handler resets the count
  only when `CONFIG_LCD_RGB_RESTART_IN_VSYNC` is off; the Arduino libraries
  turn it on (ESP-IDF's default is off), so the restart in every VSYNC
  never repaired it. Third setting: `CONFIG_LCD_RGB_RESTART_IN_VSYNC=n`.
- **The two boards' builds undid each other:** the rebuild replaces the
  framework's libraries inside `~/.platformio`, so the 4"'s next build
  reinstalled the stock framework, which the 2.1"'s next build would have
  rebuilt again. The 2.1" project now has its own `core_dir`
  (`~/.pio21`, cached in CI too). First tried inside the repo
  (`../.pio-core-2.1in`): after an hour the build failed generating the
  linker script, "The command line is too long" (Windows), as every
  framework library's full path goes on one command line; the home
  directory path is shorter than `~/.platformio`'s.
- **Getting the third setting built** took two more tries. The build with
  `core_dir = ~/.pio21` compiled the framework, then its second stage
  failed: in the fresh directory the platform had installed the newest
  PlatformIO (6.2.0.post1) into `~/.pio21/penv`, whose SCons 4.11.1 the
  platform cannot use (`No module named 'SCons.Tool.FortranCommon'`).
  With 6.1.19 installed there (`uv pip install platformio==6.1.19`) it
  linked in 7 min, reusing the framework, but the compiled `sdkconfig.h`
  still had `CONFIG_LCD_RGB_RESTART_IN_VSYNC 1`: a `sdkconfig.s3_stbd_usb`
  left in the project folder by the first build at 11:32 was taken as the
  configuration, and `sdkconfig.defaults` (where `custom_sdkconfig` goes)
  only fills what it lacks. Deleted it and the compiled framework: the
  next build (71 min) has all three.
- **Result:** the user built both units from a fresh clone of `c02a5e4` on
  another PC and flashed them over the air: the first OTA updates of both,
  and both came back on Signal K. The 2.1": no flicker and no shift, also
  while browsing the web UI's configuration pages (which shifted it within
  half a minute on the stock libraries).

## 2026-11-01 — PR #8: the 2.1" CI build

- PR #8's first CI run failed the `s3_stbd` job after 4 min with the same
  `No module named 'SCons.Tool.FortranCommon'`: the runner's fresh
  `~/.pio21/penv` got pioarduino 6.2.0.post1. The platform's
  `penv_setup.py` asks only for `pioarduino>=6.1.19` and keeps any
  installed version that satisfies it, so CI now creates the penv itself
  (same Python as `pio`) with `pioarduino==6.1.19` before the build.
  Checked locally: a venv with that pin runs PlatformIO 6.1.19.
- The next run failed the same way: `pip install --upgrade platformio`
  had put PyPI's new `platformio` 6.2.0 on the runner, and that outer
  `pio` installs `tool-scons ~4.41101` for the platform. CI now installs
  `platformio==6.1.19` for both boards; the top README's setup says so too.
- With both pins the run passed (`8cece15`): the 2.1" job built the
  framework with all three settings in 7.5 min (`Replace: ...=n` for
  `RESTART_IN_VSYNC`), 9 min in all, far inside the 90 min limit. Flash
  43.5 %, RAM 18.6 %.
- The firmware builds are now off on pushes and pull requests (too heavy
  for every change, the user's call): `firmware-build` runs only on a
  manual "Run workflow". Firmware changes are built locally, one env per
  board.
- CodeRabbit's review of `6fc791d`, all five taken:
  - the slow-pass restart counted from the first slow pass and started
    over after a minute, so passes at 0, 31, 61 and 90 s never made three
    in a minute: it now keeps the last three times (a ring) and restarts
    when the oldest is within 60 s;
  - `preview <size> bench` left the compass card at the last bench
    heading for the stale render: the sample is now applied fresh and
    settled first (stale images with and without `bench` are identical);
  - `~/.pio21` is cached on an exact `platformio.ini` match only: the
    platform cleans an old framework only under a `.platformio` path;
  - the 4" README's core-dump steps use `pio pkg exec` (esptool,
    riscv32-esp-elf-gdb);
  - the top README no longer says CI builds the firmware on every change.

## 2026-11-01 — Zones that vanished minutes after boot

- **Symptom (user):** on both units, the dials' zones disappear some time
  after boot. A saved 2.1" log shows it: the rpm, coolant and oil pages got
  their zone at 41 s, then at 153 s, within 90 ms of each other,
  `metadata applied (0 zones drawn, ...)`. The server still held the zones
  (`.../coolantTemperature/meta` over its REST API).
- **Cause:** the three paths come from another SensESP device
  (`$source` `sensesp-engines`). SensESP sends a meta delta for its outputs
  at every connect (`SKDeltaQueue::add_metadata()`, re-armed in
  `on_connected()`): `units`, `description`, no zones. The server merges it
  into what it holds and forwards it as sent to `sendMeta=all` subscribers.
  `GaugeConfig::set_meta()` built the page's metadata from each delta alone,
  so this one replaced the zones with none: the engine unit reconnecting
  wiped them on every gauge, until the gauge itself reconnected.
- **Fix:** `sk_merge_meta()` (`sk_parse.h`, tested) merges like the server:
  a field the delta carries replaces the old one (null or malformed clears
  it), one it leaves out is kept. `GaugeConfig` keeps the merged metadata
  as received (`received_`) apart from what the page draws, so the unit
  check runs on the merged result. A zone bound that is not a number now
  leaves the zone out (SensESP's `SKZone` read it as 0).
- Host tests 47 passed. Both units flashed over USB (`p4_stbd_usb`, `s3_stbd_usb`):
  zones on the rpm, coolant and oil pages at boot, none lost in the first
  8 (2.1") and 13 (4") minutes. Not yet seen: the engine unit reconnecting
  while a gauge runs, the case that wiped them.
- CodeRabbit's full review of `1c34764`: `bench` counted frames that
  redrew nothing (the first, at the settled sample), so "fastest" could be
  a no-op (0.01 ms). It now times only frames that flushed a strip, and
  prints how many did (239 of 240, the compass 237).
- CodeRabbit's review of `8e13ffd`: no actionable findings, one nitpick:
  the merge was tested on a bare `GaugeMeta`, not through
  `GaugeConfig::set_meta()`. `set_meta()` is SensESP glue the host tests
  cannot build, so its unit check moved to `meta_fits_unit()`
  (`gauge_resolve.*`), and a test runs `set_meta()`'s steps on the port
  rpm dial: zones, then SensESP's `{units, description}`, still draws
  both zones; a meta in another unit draws none, and back in Hz the zones
  return. With each delta taken as the whole metadata (the bug before
  `1c34764`) it fails.

## 2026-11-02 — The 4"'s slow-pass restart hung on the dead C6

- **Seen (user):** the 4" restarted, "Last reset reason: Task watchdog",
  about 19.5 h after the 2026-11-01 USB flash (uptime 4.5 h when read).
- **Core dump:** the loop task in `esp_restart()` (`gauge_app.cpp`, the
  slow-pass restart) -> shutdown handler -> `rpc_wifi_stop()` ->
  `wait_for_sync_response()`. The C6 had stopped answering, three SensESP
  passes of 4 s or more in a minute fired the restart as intended, and
  ESP-Hosted's shutdown handler then asked the C6 to stop WiFi and waited;
  the 12 s loop watchdog reset the unit. It recovered, 12 s late and
  labelled as a watchdog reset. DMA RAM was not short (78 KB free after
  the reboot); why the C6 stopped answering is not known.
- **Fix:** that restart calls `esp_restart_noos()`, the rest of
  `esp_restart()` without the shutdown handlers. Registered ones, from the
  libraries' symbols: `esp_wifi_remote` (the WiFi stop), `esp_timer`, and
  TinyUSB's in the Arduino core (not used: USB Serial/JTAG). ESP-Hosted
  resets the C6 at every boot (`CONFIG_ESP_HOSTED_SLAVE_RESET_ON_EVERY_HOST_BOOTUP`,
  GPIO 54), which is why the unit came back.
- The 4" README's core-dump steps named the GDB package wrong: it is
  `tool-riscv32-esp-elf-gdb`.

## 2026-11-02 — Fixes adopted from espOS

[espOS](https://github.com/signalk-espOS/espOS) is an ESP-IDF 6 runtime for
Signal K devices, in daily use on ESP32-P4 panels. A review of its fixes for
what applies to these gauges (SensESP, Arduino-ESP32 3.3.11):

- **Split WebSocket messages** (15 lost on the 2.1" in 3.5 h, 1 on the 4";
  8 within one second during a burst at 23:00): esp_websocket_client posts
  a message larger than its 1 KB buffer, or one TCP split, in pieces, and
  SensESP parsed each piece as a whole message (`deserializeJson error:
  IncompleteInput` then `InvalidInput`). espOS joins frames up to 16 KB.
  Fixed in SensESP itself (the user's fork, branch
  `fix/ws-message-reassembly`, commit `5ff25ad`): `SKWSMessageAssembler`,
  13 native tests; messages up to `SENSESP_SK_WS_MAX_MESSAGE_SIZE` (4096).
  Both boards build from that branch.
- **The C6's heartbeat** (espOS `hosted_watchdog.c`): the 4" asks for one
  every 20 s; 60 s without one restarts it. espOS found a wedged SDIO link
  produces no error at all, so only an absence shows it. A C6 start after
  the first heartbeat restarts too (the one at boot does not: espOS
  boot-looped a P4 on it), and so does `TRANSPORT_FAILURE`. Our restart uses
  `esp_restart_noos()`; espOS calls `esp_restart()` from tasks no watchdog
  watches.
- **Signal K stalled with WiFi up** (espOS `skLinkStalled`): 5 min, only
  after it worked this boot, so a server that is down does not loop the
  gauge.
- **SensESP's WiFi watchdog** turned on (restart after 3 min without the
  network, armed by the first connection), covering espOS's `wifiStalled`.
- **WiFi power save off**: Arduino's default (modem sleep) lets the access
  point hold frames back and deliver them in bursts; espOS runs without.
- **Lowest free DMA RAM** in the heap line (espOS `memoryTrough`).
- Rules in `link_policy.h`, 7 host tests (56 in all).
- **Not taken, for now:** ESP-Hosted 2.12.12's fix for a dead link going
  unnoticed (esp-hosted-mcu #220) needs Arduino-ESP32 3.3.12 and the C6
  updated to 2.12.13, a separate step; espOS's link settings
  (`WIFI_RMT_RX_BA_WIN=6`, ESP-Hosted's buffers in PSRAM) need a 4"
  framework rebuild; the C6 updated over its own link (we have
  `tools/c6-update`); core dumps over HTTP.
- **On the units** (2026-11-03, USB): all three flashed (`p4_stbd_usb`,
  `p4_port_usb`, `s3_stbd_usb`) with SensESP `5ff25ad`. Both 4" log
  `link: Watching the WiFi chip (C6)` at about 6 s and `First heartbeat`
  20 s later: the C6 on 2.12.11 sends heartbeats, and so does the port
  unit's C6 on its factory firmware. All three back on Signal K with their
  zones. Lowest free DMA RAM since boot: 2.1" 43 KB, 4" 53 KB.
- **The 4" port unit's C6 still runs its factory firmware**: `Slave firmware
  version: 0.0.0`, `Version on Host is NEWER than version on co-processor`,
  and the version request goes unanswered (`Response not received for
  [0x15e]`). That is the state `tools/c6-update` exists for; the unit had
  been failing to reach Signal K before this flash.
- A build mistake on the way: `esp_hosted_misc.h` has no `extern "C"`
  guard, so the heartbeat call did not link from C++ until the includes
  were wrapped.
- **The port unit's C6 updated** to 2.12.11 with `tools/c6-update` (USB,
  COM6). The first run was interrupted from the PC side but had already
  written and activated the image; the second found it up to date. Gauge
  flashed again: `Versions Match!`, heartbeats from 25.8 s, back on Signal
  K with its `prt_engine` pages, so its settings survived (the gauge's
  filesystem is at 16 MB, beyond the tool's default partition table).
- The C6 update is now a section of its own at the top of the 4" README,
  with a step in the top README's setup, instead of a checklist bullet:
  both boards shipped with the factory C6 firmware.

## 2026-11-03 — A dead C6 link, recovered by the new restart

- **06:43:00**, the 4" starboard at 6.6 h of uptime: from one request to
  the next, the C6 answered nothing (`Response not received for
  [0x126](Req_WifiStaGetApInfo)`, SensESP asking for the signal strength).
  Nothing unusual before it; DMA RAM 77 KB free, lowest 53 KB.
- Each SensESP pass then held the loop 10 s (two 5 s timeouts), and the
  third made the slow-pass rule restart the unit at **06:43:30**:
  `SW_CPU_RESET`, a clean restart through `esp_restart_noos()`, no hang in
  the shutdown handlers as on 2026-11-02. The heartbeat watchdog would have
  fired by about 06:44; the slow-pass rule came first.
- Back: `Versions Match!` at 5 s, first heartbeat at 25.7 s, Signal K and
  live readings with zones at **06:44:13**, about 70 s after the link died.
- The third dead link on a 4" (about 6 h and 19.5 h before, on 2.12.11
  each time): the restart recovers, it does not prevent. ESP-Hosted
  2.12.12 fixes the failure going unnoticed (esp-hosted-mcu #220);
  Arduino-ESP32 3.3.12 ships 2.12.13.
- Close call: two unanswered requests per pass made 10 s passes against
  the 12 s loop watchdog; a third request in one pass would have been a
  watchdog reset instead (still a restart, with a core dump).
- **07:29:35, the 4" port** at 7.0 h of uptime, the same way: every
  request to the C6 unanswered, three 10 s passes, slow-pass restart at
  07:30:05 (`SW_CPU_RESET`), `Versions Match!`, first heartbeat at 25.7 s,
  Signal K back at 07:30:44 (about 70 s). Its C6 had been on 2.12.11 since
  00:31. Both 4" units dead-linked within 50 min of each other, at 6.6 h
  and 7.0 h: on ESP-Hosted 2.12.11 it looks like a regular event every 6-7
  hours of running, recovered each time, which makes the move to 2.12.13
  (Arduino-ESP32 3.3.12) worth doing.

## 2026-11-03 — All three units on Arduino-ESP32 3.3.12 (ESP-Hosted 2.12.13)

- **Why:** both 4" units lost their C6 link every 6-7 h on ESP-Hosted
  2.12.11; 2.12.12 fixes a dead SDIO link going unnoticed
  (esp-hosted-mcu #220). pioarduino 55.03.312-1 = Arduino-ESP32 3.3.12,
  ESP-Hosted 2.12.13 (its P4 `dependencies.lock`). The user chose to move
  both boards, so one toolchain builds both.
- **PlatformIO:** 55.03.312 needs PlatformIO Core >=6.2.0 (6.1.19:
  `IncompatiblePlatform`). pioarduino's newest, 6.2.0.post1, reports
  `6.2.0-post.1`, a pre-release below 6.2.0, and is refused the same way;
  `pioarduino==6.2.0` alone (no `platformio` package beside it) works, in
  `~/.platformio/penv` and `~/.pio21/penv`. CI pins it too.
- **A mistake on the way:** the two boards built at once on the
  just-upgraded PlatformIO both installed the platform's Python packages
  into `~/.platformio/penv` and broke it (`certifi` removed, `Failed to
  install Python dependencies into penv`). Repaired with uv; one build at
  a time since.
- **C6 image 2.12.13** pinned in `tools/c6-update`: 1226976 bytes, SHA-256
  `f8509949...5121c9`, identical from espressif.github.io and from
  raw.githubusercontent.com (gh-pages).
- **4" starboard and port:** C6 updated 2.12.11 -> 2.12.13 (download,
  checked, written, confirmed after its restart), gauge flashed:
  `Versions Match!`, display and touch as before (0x14 and 0x5D), first
  heartbeat at 26 s, Signal K with zones. Internal DMA RAM 289-290 KB free
  a minute after boot (78 KB on 3.3.11; internal RAM at the draw buffer's
  allocation 453 KB against 325 KB). New at boot only: a few
  `rpc_wifi_sta_get_ap_info: failed, status [12303]` while WiFi
  associates, answered at once (not timeouts).
- **2.1":** framework rebuilt with the three `custom_sdkconfig` settings
  (stale `sdkconfig.s3_stbd_usb` deleted first), 1 h 28 min in all; the
  `qio_opi` variant's `sdkconfig.h` has `XIP_FROM_PSRAM` and no
  `RGB_RESTART_IN_VSYNC`. Flashed: Signal K with zones, DMA RAM 55 KB free
  (lowest 32 KB), PSRAM 3456 KB (code from PSRAM, as before). The picture
  (no flicker, no shift) is still to be confirmed by eye over time.
- Builds: 4" 31 min (full), C6 tool 3 min, 2.1" 1 h 28 min.

## 2026-11-03 — One firmware per board; a fourth unit

- **Why:** a new 2.1" port unit (COM9) needed `s3_port_usb`, which
  compiled everything (~22 min) although `s3_stbd_usb` was built: the
  side, hostname, name and address were global `-D` flags in every compile
  command, and each env has its own `.pio/libdeps/<env>` on the include
  path, which the build cache keys on. Four envs per board, four full
  builds.
- **Now:** one env per board, `p4` and `s3`. The side comes from the
  hostname saved on the unit (SensESP's `/system/hostname`; `-port` suffix
  = port, `unit_identity.h`, 3 host tests), which picks the factory pages'
  engine, the unit name and its `secrets.h` address. The upload port picks
  the method: `COMn` USB, `port` / `stbd` that unit's
  `SECRET_OTA_HOST_<ENV>_<SIDE>` over WiFi (`ota_auth.py`), an IP or
  `.local` name OTA.
- **Migration:** before the new firmware, each unit's current hostname
  was written back through the config API so it is saved (a unit that
  never changed it had none on file, and would have come back as
  starboard).
- **A crash on the way:** reading the hostname with SensESP's
  `PersistingObservableValue` before the app existed panicked at every
  boot (`LoadProhibited` in `event_loop()`, from its constructor). Read
  with a bare `FileSystemSaveable` instead.
- **The fourth unit, 2.1" port:** flashed, hostname set to
  `gauge-21-port`, its six cards written through the API from the 2.1"
  starboard's with `stb_engine` -> `prt_engine` (each with the unit's own
  `_rev`, which a save must carry), restarted: `2.1in PORT
  (gauge-21-port)`, zones, live `prt_engine` readings.
- **All four units** on the single firmware, each with the right identity.
- **Build time, measured on the 4":** a build with nothing to compile took
  139 s: 10 s start, 47 s PlatformIO's library scan, 80 s SCons checking
  ~1000 objects. `scripts/fast_scons.py` (`Decider("MD5-timestamp")`,
  `implicit_cache`) took the last part to 11 s: 66 s in all. One changed
  file: 198 s before. Any edit of `platformio.ini` deletes the build folder
  (PlatformIO's `project.checksum` covers the whole config), and the
  objects come back from the cache: 190 s on the 4".
- **The Unit card:** a Starboard / Port dropdown in the web UI
  (`unit_card.*`, labels in `unit_identity.h`, tested). It shows the side
  of the current hostname and, on a change, sets SensESP's hostname to
  `gauge-<board>-stbd` / `-port`; the web UI asks for the restart that
  applies it. Tried on the 2.1" port: Starboard -> `gauge-21-stbd`, Port
  -> back, an invalid value refused (400). The docs now say "choose Port
  in the Unit card" instead of editing `/system/hostname`.
- **OTA by side:** the 2.1" port, the 2.1" starboard and both 4" units
  were updated over WiFi with `--upload-port port` / `stbd`: 99-106 s
  each from a built firmware, 174 s with two files to compile.
- **Firmware version 0.1.0** from a `VERSION` file at the repository root:
  `scripts/gauge_version.py` (pre:) writes the gitignored
  `common/src/gauge_version_value.h`, which only `gauge_version.cpp`
  includes, so a new version recompiles one file. Shown in the boot line
  (`firmware 0.1.0`), on the info overlay (`Firmware 0.1.0`) and on the
  web UI's Status page ("Gauge firmware", Software). All four units
  updated over WiFi and report 0.1.0 (boot log and `/api/info`).
- **`docs/ADDING_GAUGES.md`**: how to add a page (web UI), another unit
  of a board (flash the existing build, Unit card, Signal K approval,
  pages; a third unit by a hostname of its own), a new page style (the
  files to change, in order, with tests and the preview) and a new board;
  and where the factory pages are. Linked from the README and AGENTS.md.
- **`tools/copy-pages`**: copies a unit's Pages card and page cards to
  another over the web API, `--replace` for the engine paths and titles,
  each card with the target's `_rev`, `--restart` when the target needs
  one (new page slots exist only after a restart with the new count, and
  page settings apply at the next restart), `--dry-run`. Tried from the
  2.1" starboard to the 2.1" port (`stb_engine` -> `prt_engine`): six
  cards written, 200 each. `ADDING_GAUGES.md` section 1 now walks through
  an example page (exhaust temperature) setting by setting.

## 2026-11-03 — General role; everything about a unit set in the Unit card

- **Asked:** a unit used for something other than a starboard or port
  engine display, and nothing hard-coded but the version.
- **The Unit card** now has three fields, all saved on the unit: **Role**
  (Starboard, Port, General), **Hostname** (SensESP's own setting) and
  **Display name**. The role and display name are in `UnitSettings`
  (`/Unit/settings`), read at boot before SensESP; the hostname stays
  SensESP's. A built-in hostname (`gauge-<board>-stbd` / `-port` / `-gen`)
  follows a role change; a name of the user's own is kept. A unit with
  no role saved takes it from its hostname (`-port`, `-stbd`, else
  General), so the four units kept what they were.
- **General** units have their own factory pages, with no engine: speed,
  heading, depth (0-50 m), apparent wind (0-40 kn), heel, battery (10-16 V,
  `electrical.batteries.house.voltage`); ticks from `nice_step()`. No
  `secrets.h` address of their own (DHCP); `ota_auth.py` takes any short
  `--upload-port <name>` that has a `SECRET_OTA_HOST_<ENV>_<NAME>`.
- Rules in `unit_identity.h` (host-tested: 62 tests), the General pages in
  `test_factory.cpp`, the three new dials rendered in the preview at 480
  and looked at (the old hand-made "depth"/"battery" demo dials renamed
  `ui_depth`/`ui_battery`).
- **On the 2.1" port:** General with the hostname unchanged -> boot
  `2.1in GEN (gauge-21-gen)`; own names -> `Helm NAV (gauge-21-nav)`;
  back to Port -> `2.1in PORT (gauge-21-port)`, its six pages untouched
  throughout; a bad role, hostname or display name refused (400). All four
  units run it.
- **SensESP `b29c837`** (the fork branch, after review on
  SignalK/SensESP#1097: `fin` found by the event struct's members, a JSON
  end scanner where a client has no `fin`; the joining buffer kept between
  messages, up to ~4 KB). For our ESP-IDF 5 builds the message path is
  unchanged; 20 assembler tests pass. All four units run it.
- **A trap on the way:** `pio pkg update -e p4 -l SensESP` rewrote the 4"'s
  `platformio.ini` (every comment gone, and `lib_deps = SensESP@...` in
  `[env:p4]`, overriding the list in `[env]`) and left `.pio/libdeps/p4`
  with only `integrity.dat`: the build failed with
  `esp_display_panel.hpp: No such file or directory`. On the 2.1" the
  same command failed (`git pull --recurse-submodules`). Restored the file
  from git and reinstalled the libraries (`pio pkg install -e p4` after
  removing the folder). Now in AGENTS.md: never `pio pkg update`; to take a
  new SensESP commit, delete `.pio/libdeps/<env>/SensESP` and build.

## 2026-11-06 — Crew Radio on the 4": display mock-ups

- Proposal (in conversation, not yet a document): a FreeRTOS Crew Radio
  node for the ESP32-P4 board (ES8311 + NS4150B speaker, ES7210 mic), the
  radio core in the crew-radio repo as a library this firmware would link.
  Nothing in the firmware uses it yet.
- Mock-ups of what it would show, drawn with LVGL in `common/src/ui` so
  the preview renders them at 480 and 720 (`out/radio*`):
  - **Halo** (`radio_halo.*`): over any gauge page, an opaque band over the
    bezel (outside the face's 0.85 R, so no reading is covered) while
    someone talks, the name set along the top arc glyph by glyph
    (`ArcText`), green solid when heard directly, dashed through a relay or
    over Reticulum, grey when muted, red with `TALKING` / `n LISTENING`
    while this unit transmits. Otherwise only a radio pip left of the
    status dot (green on channel, amber without a clock, slashed muted).
  - **Crew page** (`crew_page.*`): the roster as a compass, pips on a ring
    open at the bottom, name and the phones' 1-4 bars under each, the
    talker glowing; channel name, head count in the gauges' LCD (dashes
    when not on channel) and one status line in the middle; volume arc at
    the bottom. Station variant: the talk disc in the middle and the
    channel name engraved in the bezel's chrome.
- Looked at both sizes: names moved under their pips (radially they hit the
  side pips), the member span cut to +/-118 degrees (the lower bars hit the
  volume's -/+), the engraved name moved out onto the bright chrome (dark ink
  vanished on the bevel), the halo band made opaque (the engraving showed
  through). At 480 the names are at the 12 px font floor; the radio is for
  the 4" only.
- Montserrat here has ASCII and LVGL's symbols only: a name with "a" with
  umlaut, "·" or "●" would not render; the mock-ups avoid them, the
  real one must map or substitute.

## 2026-11-06 — P4 Gauge Crew Radio, phase 0: the core

- New folder `p4-gauge-crew-radio/` (its own PlatformIO library,
  `P4GaugeCrewRadio`, EUPL-1.2): a Crew Radio node for the 4", wire v4
  compatible with the crew-radio app. Here rather than in the crew-radio
  repo, by choice; the design, the FreeRTOS tasks, the safeguards, the
  Reticulum plan and the phases are in its `README.md`.
- Phase 0: `src/core` ports `Packet`, `Hello`, `ChannelCrypto` (PBKDF2 by
  hand over mbedTLS HMAC, AES-GCM, the Reticulum tag and confirm key),
  `RateLimiter` (integer thousandths of a token: the P4's FPU is single
  precision, and the app's arithmetic is exact that way), the seen-cache
  (fixed memory: linear probing, backward-shift deletion, an insertion-order
  ring), `SeqTracker`, `Ingress`, `Conceal` and `Mixer`. No locks: on the
  node one receive task owns the pipeline and the playout task owns the
  mixer, where the app has a thread per transport.
- 57 host tests (`-Werror`, ASan, UBSan), against the app's own
  `vector.json`/`rns.vector.json` (copied from crew-radio `472f624`): the
  derived keys, the hello and the sealed packet byte for byte, first run;
  PBKDF2 also against Python's hashlib. A wrong salt fails the vector test
  (checked). mbedTLS 3.6.4 downloaded by the build, pinned by SHA-256 (taken
  from the downloaded release; mbedTLS's own published sum not compared).
  CI job `crew-radio-tests`.
- Not built for the board yet: nothing links the library; phase 1 (the
  station) does.

## 2026-11-06 — P4 Gauge Crew Radio, phase 1: listening, in the 4" firmware

- No separate station firmware after all: one firmware per board is this
  repo's rule (a second env compiles everything again). The radio is linked
  into the 4"'s `p4` env (`platformio.ini`: the library by symlink, and
  pschatzmann/codec-opus pinned to `c02de63`) and started from its
  `main.cpp`; off until switched on in its "Crew radio" card. The station is
  a 4" with the radio on and the Crew page (phase 3).
- Core, host-tested (89 tests, +2 with libopus): `lan_peers` (the app's
  unicast/group/burst-floor rule), `roster`, `clock_policy`, `supervisor`,
  `memory_guard`, `self_test` (the vector packet compiled in), `spsc_ring`
  (two-thread test), `decoder_pool`, `opus_decoder`, `playout`, `net_node`
  (the whole receive side without sockets), `radio_settings` (the app's key
  rule).
- The GNSS clock: Signal K resends a path's last value on every subscribe
  and keeps a dead receiver's last time for ever, so a reading counts only
  when it advanced by the real time since the one before (within 2 s,
  0.5-60 s apart). Noticed while wiring the listener; the first version would
  have set the clock to the retained time.
- Board side (`src/esp`, `src/glue`): ES8311 set up from esp_codec_dev's
  register values (Apache-2.0) over I2C port 0 through the legacy driver,
  the one ESP32_Display_Panel installs for touch (the new driver beside it
  aborts at boot); I2S std, mono, 16 kHz, MCLK 256 fs, 3 DMA buffers of one
  frame; amplifier enable after the codec (no pop). Tasks on core 0:
  `cr-play` 17, `cr-net` 16, `cr-beat` 3. SNTP from the gateway, then
  pool.ntp.org, started only if nobody started it. The card derives the
  packet key at save (PBKDF2 in the web server's task, pausing every 2000
  rounds for the idle task) and keeps it in NVS; never the channel key.
- Built: `pio run -e p4` succeeds here (pioarduino 6.2.0), flash 2.97 ->
  3.12 MB, static RAM +1.5 KB (libopus's 60 KB scratch is allocated at run
  time, so PSRAM). Not run on a board: the first power-up checklist is in
  the radio's README. Not built here: the 2.1" (its framework rebuild takes
  over an hour); its only change is the mock-up UI code, which the preview
  compiles.
- Building libopus for the host tests was not allowed in this session's
  sandbox (third-party code), so `CREW_RADIO_OPUS` (on by default, as in
  CI) gates them; the other 89 ran here.

## 2026-11-06 — The repository under EUPL-1.2

- `LICENSE` at the root: EUPL-1.2, the same text as crew-radio's, for the
  whole repository (it had none; `common/library.json` said MIT, now
  EUPL-1.2). README "Licence and credits" names the dependencies and their
  licences, all permissive and compatible; AGENTS.md says to keep it.

## 2026-11-06 — P4 Gauge Crew Radio, phase 2: talking

- Core, host-tested (97 tests, +3 with libopus): `talk_key` (40 ms to key,
  60 ms to let go, and a 90 s time-out that keeps the mic cut until every
  input is released, as a VHF set's: a shorted button or a resting hand
  would otherwise hold the channel for the whole crew), `transmitter` (its
  packets open on another NetNode as this node's voice, numbered), the
  encoders (PCM, and Opus at 24 kbit/s VoIP, complexity 5), `BurstFloor`
  (taken out of LanPeers so the audio task keeps its own), `peer_snapshot`
  (a seqlock of atomics: cr-net publishes, cr-audio reads, nobody waits;
  tested over two threads), `tones` (key beeps), `mic` (channel, level, the
  ES7210 gain code as esp_codec_dev rounds it).
- Two of the first test runs failed on the tests, not the code: the burst
  gap arithmetic was a frame off, and the snapshot test read its own
  deliberately mixed starting view before the writer thread ran.
- Board side: the ES7210 from esp_codec_dev's register values (slave, MIC1 +
  MIC2, 16-bit I2S); I2S full duplex on one controller with the same stereo
  slots both ways; the playout task became `cr-audio` (play, capture, key,
  encode, seal, send with its own socket; every libopus call there); half
  duplex (the mixer's gain to 0 while keyed). Without an ES7210 the unit
  still listens.
- `common/src/lvgl_port.cpp` reads two touch points: while two are down
  LVGL is told the screen is released (no swipe, no brightness hold) and
  `touch_fingers()` says 2. The 2.1"'s touch reports one point, so nothing
  changes there.
- Card: two-finger talk, talk button GPIO (the board's own pins refused),
  Opus, mic gain, microphone (MIC1/MIC2, an unknown one refused), key beeps;
  a "Radio talk" status line.
- `pio run -e p4` builds: flash 3.12 -> 3.24 MB. Not run on a board: the
  radio's README has the talking checklist (items 9-15).
- CodeRabbit's first review (of the first three commits), both fixed here:
  a task that could not be created went unnoticed (the supervisor never saw
  its slot, and the status could say "On channel"): now both start or
  neither, and the radio shows "Stopped"; and the arc text's last glyph
  skipped the width check, so a long name could run past its arc: now the
  whole width is checked first and a name that does not fit is cut with
  "..", no space before them (a new preview scene, `long_name`).
- Asked for the exact boundary (the prefix and ".." fit, the prefix and the
  last glyph do not), the arc rule moved into `common/src/arc_fit.h`, free
  of LVGL, with `tests/test_arc_fit.cpp`: whole when it fits, cut at that
  boundary, never past the arc for any length and last width, no space
  before the dots. The sweep found one more edge: a text exactly as long
  as its arc was cut by float rounding (px -> degrees); a thousandth of a
  degree of slack fixed it.
- CodeRabbit's follow-up on the task fix: a cr-beat that could not be
  created left the status at "No clock" (only cr-beat ever wrote Stopped),
  and a task that did not stop within 2 s went unsaid. Now every start
  failure, cr-beat's included, stops the tasks and reports Stopped at once,
  and a task that does not stop is logged (it is never restarted). The
  state rule (`running_state`: Stopped whenever a task is not running) is in
  `core/radio_state.h`, tested; FreeRTOS task creation itself cannot fail on
  demand on the host, so the paths around it are checked by the build.

## 2026-11-06 — The Crew Radio work split in two PRs

- One PR had reached 118 files; CodeRabbit reviews at most 100. Split by
  area, as the maintainer chose: the radio library `p4-gauge-crew-radio/`
  with its CI job (94 files, PR #9), and stacked on it the gauge side (this
  entry's PR): the display mock-ups and `arc_fit.h`, the two-finger touch in
  `lvgl_port`, the 4" firmware hookup (`platformio.ini`, `main.cpp`), the
  EUPL-1.2 licence for the repository, and the docs. Together they are the
  same tree that was tested (host tests, preview, `pio run -e p4`).

## 2026-11-06 — P4 Gauge Crew Radio, phase 3: on the screen

- The radio library (PR #9's branch): `core/link_quality.h`, the app's
  LinkQuality rule for rule (missing hellos of the last ten, lost audio of
  the last five seconds of talk, the worse; an overdue hello counts as
  missing; the gap before an entry's first packet does not). The roster
  keeps one per node and its members carry their bars: the Crew page shows
  measured bars only, never made-up ones. A Channel name in the card.
- The gauge side: a fourth page style, **Crew radio** (`GaugeKind::kCrew`,
  no path, nothing subscribed), built by `create_page()` as
  `CrewGaugePage`, which draws the Crew page into the gauge page's own root
  and canvas and polls the radio's view four times a second, redrawing only
  on a change. The halo sits on the top layer under the status dot, made
  only where a radio is registered (`set_crew_view_source()`, set in the
  4"'s `main.cpp` before the UI is built; `src/crew_view_source.cpp` maps
  the radio's status to the UI's view, the one place that knows both). The
  info box gains a Radio line. Without a source the page says RADIO OFF.
- Not faked: no mute (the radio has none yet), no "speaker" pip (nothing on
  the wire tells the plugin from a person), "over Reticulum" only for a
  relayed member whose hello says so.
- Checks: gauge tests 68 (the new style's rules), radio 112, preview at
  both sizes (the Crew page now through the page system), `pio run -e p4`
  (flash 3.26 MB). Not run on a board.
- Next: the station's talk disc, mute and volume drag (3b); Reticulum (2b)
  in its own PR (the library PR is at 95 files).

## 2026-11-06 — P4 Gauge Crew Radio, phase 2b: Reticulum

- A third PR, stacked on the gauge side's, because the library PR is at 95
  files. `p4-gauge-crew-radio/src/core/rns/`: the app's `rns/` package rule
  for rule, checked against the same `rns.vector.json` (copied into
  `tests/vectors/`) that the app and the plugin read. Packets and HDLC
  framing, Reticulum's crypto (the CBC token's padding removed only after
  the MAC), identities and announces, links, `Carry` and its key proof,
  rnsd's announce budget, and the node: announces within that budget, the
  lower hash dials and the other takes over after 5 s, nothing on a link
  until the far end proves the key, flood budgets before any signature
  work, evictions that never touch a confirmed link. The plugin's two
  dead-connection rules as well, since lwIP has no `TCP_USER_TIMEOUT`.
- X25519 and Ed25519 by hand (`curve25519.h`), ported from the app's
  `Curve25519.kt`: mbedTLS 3.6 has no Ed25519, and the design the README
  had planned (Monocypher) would have been third-party code to vendor. The
  constants are written out and the tests derive them again; RFC 7748 and
  RFC 8032 vectors pass. On a PC a signature check takes 1.8 ms; on the P4
  some tens of milliseconds are expected, hence the task below.
- The 4": `cr-rns` (priority 5, under `cr-net` and `cr-audio`) owns the TCP
  socket and the node; two PSRAM ring buffers carry the sealed packets to
  and from `cr-net`, so `Ingress` still has one caller. `NetNode` bridges:
  a first copy, or one that reaches further, goes to the other transport
  with `relay_ttl`, never back. Hellos say Reticulum while connected; a
  member heard over it is drawn dashed. Connected only with a trusted clock
  and while the memory guard leaves Reticulum on. The card: Reticulum,
  Transport node (`host[:port]`, the app's rule), Relay; a status line.
- Checks: radio host tests 153 (156 with libopus), `pio run -e p4` (flash
  3.29 MB, no warnings from the new files). Not run against rnsd or on a
  board; items 16-22 of the README's first power-up list are for that.

## 2026-11-06 — The radio library: CodeRabbit's full review

- Peer learning (security): an address is learned only from a fresh first
  copy Ingress accepted, never from a copy or a stale packet, and the
  table holds one address per authenticated sender id, so a replayed
  packet, even a fresh one this unit missed, can move only its own
  sender's entry until that sender's next hello. Both stricter than the
  app.
- The mixer no longer reserves again the slots it already concealed on a
  dry queue when a later frame reports them lost (the app's Mixer does,
  holding that frame back by as many slots). The supervisor reads a
  slot's armed flag before its time. `library.json`: Arduino only.
- Checks: radio host tests 117 on the library branch, 158 with Reticulum;
  `pio run -e p4` after the first round.

## 2026-11-07 — The gauge side: CodeRabbit's review

- Touch: after a two-finger hold LVGL sees a release until every finger
  is off, so lifting one of the two no longer swipes or taps with the
  other. `touch_fingers()` still follows every read.
- Names: Montserrat has ASCII only, and "Mäkelä" drew as "Mkel".
  `text_fold.h` (host-safe, tested) spells names in ASCII (Makela, ß ss,
  Æ AE, anything else '?'); `crew_view_source` folds the members' and the
  channel's names.
- README: the EUPL's compatible-licence option as Article 5 states it.
- Checks: gauge tests 72, preview built, `pio run -e p4`. The 2.1" shares
  the `lvgl_port` change and was not built.

## 2026-11-07 — Reticulum: CodeRabbit's review

- `reticulum_link`: the shared state is published to the other tasks only
  once whole (an atomic pointer, its lock first): `cr-net`'s first status
  read could otherwise take a lock not yet made. The status note is set
  with each state, so an old failure never lingers.
- `cr-net` without the WLAN socket keeps its turn: Reticulum taken, the
  hello there without the LAN flag, the roster expired and published, the
  socket retried twice a second at most.
- `parse_host_port`: an IPv6 node must be an IPv6 address (RFC 4291's text
  forms), stricter than the app.
- Checks: radio host tests 159, `pio run -e p4`.

## 2026-11-07 — The crew branch: a self-review in six parts

Six independent reviews of everything on `crew` (Reticulum primitives;
the Reticulum node and its socket task; the wire and the receive
pipeline; the audio path and the codecs; the ESP runtime and the card;
the gauge side), each finding checked against the code, the app and the
plugin before it was fixed.

- Security:
  - One ingress budget per WLAN source address, as the app's
    `SourceLimiter`: one host with no key could take the whole global
    budget and silence the crew.
  - HMAC is now written over SHA-256 with no allocation, and every
    mbedTLS failure is refused. Before, an allocation failure left the MAC
    as stack garbage, which a token check then compared against.
  - On the P4, randomness comes from an HMAC-DRBG seeded with the ADC
    entropy source on (the P4 has no radio of its own).
  - The talk GPIO no longer accepts the C6's SDIO pins, which could take
    the WiFi down for good.
  - Peers are learned only from direct copies that move the sequence on.
- Behaviour, now as the app:
  - The roster's bars before a member's first hello.
  - Decoder take-over only after 2 s of silence.
  - No concealment of gaps over three frames.
  - Opus at constant bitrate.
- Robustness:
  - The talk lock-out survives a moment when talking is not allowed, and
    a stale touch sample counts as a release.
  - `cr-rns` runs below the guard. A trickling uplink, a shed connection
    attempt and an oversized packet are each handled.
  - The radio's lock and queue are checked, and the I2S DMA is stopped
    when the audio start fails.
  - A `recvfrom` error waits before the socket is reopened.
  - The mic queue is emptied at start.
- Screen:
  - New `No network` state.
  - A radio whose status is more than 5 s stale is shown as stopped.
  - The halo follows the talk at once, only redraws its own band and pip,
    and skips arcs the refresh cannot touch. A lit dashed halo had made
    each needle frame about 5x dearer.
  - The halo's glyphs are in PSRAM on the 4".
  - The weakest-link bars show only on channel.
  - The Crew page cuts names to fit.
  - A single "no new data" read from the GT911 is not a release.
  - The Crew style's card offers only what it uses.
- Not changed, recorded:
  - libopus's assertions. They are in codec-opus's `config.h`; none trips
    at the fixed 24 kbit/s.
  - The ES7210 gain rounding at 33 dB, and the ES8311's unused ADC output.
    Neither can be checked here.
  - `sendto` from `cr-audio`.
- Checks: radio host tests 176 (OPUS off), gauge tests 73, the preview at
  480 and 720 (two new scenes), `pio run -e p4`.

## 2026-11-07 — The relayed vector from crew-radio

- crew-radio #67 (merged as `3ad26c4`) added a `relayed` entry to
  `sk-plugin/test/vector.json`: the vector packet as it arrives two relays
  on, ttl 2, hops 4. In the original packet ttl == hops, so a parser that
  swapped the two bytes passed on every side.
- `tests/vectors/vector.json` is copied again (`rns.vector.json` is
  unchanged). The README points at the new commit.
- New test `vector_relayed_packet_opens_with_its_ttl_left_out_of_the_aad`
  checks four things:
  - the packet parses to ttl 2, hops 4;
  - it opens with the AAD this node builds;
  - an AAD that zeroes the hops byte instead does not open it;
  - `set_ttl(2)` on the sender's packet gives these exact bytes.
- Checks: radio host tests 178 (OPUS off).

## 2026-11-07 — The crew branch on the 4" starboard unit

`pio run -e p4` (3.30 MB, 39.6 %), OTA to 192.168.0.33, watched over the
UART log and the Status page.

- Boot as before the radio: display, touch (0x14), C6 at 2.12.13, Signal
  K, six pages with readings. The pin-255 / LEDC timer errors and the
  ~20 s of `NO_AP_FOUND` (the first SSID in secrets.h is out of range) are
  in every boot since 11-03; not the radio's.
- Radio on: `cr-net`, `cr-audio`, `cr-beat` with stack to spare, about
  210-235 KB of DMA RAM free, nothing shed. With the crew's key and
  Reticulum through the hub: 2 on the roster, 2 links, packets in with
  none rejected, 760 frames sent then back to listening, 148 frames
  played (4 concealed, no decode errors).
- A channel key save takes 22 s (PBKDF2, 600 000 rounds), and the loop
  is held about 210 ms at a time while it runs. The card said "a few
  seconds": now "about 20 s: wait for it" (card, its header, README).
  Built, not yet flashed.

## 2026-11-07 — Speaker volume and mic gain from the Crew page

Asked for: both levels settable on the gauge, not only in the web card at
the next restart.

- The Crew page's volume arc is a control while the radio runs: a tap in
  the bottom of the dial left of the label lowers, right of it raises; a
  tap on the label switches the arc to the mic gain (green, `MIC 30 dB`)
  and back, by itself after 10 s. A toast says the new level. Taps, not
  dragging the arc (the plan for 3b): a sideways drag is a page swipe.
- Steps (`core/levels.h`, host-tested): the volume in 5s, 0 mutes (the
  Crew page and the halo show MUTED); the mic gain one ES7210 PGA step,
  from the gain the codec runs at (a card's 32 dB runs at 30).
- `esp::set_volume` / `set_mic_gain` store atomics; `cr-audio`, which
  owns the codecs, writes them within a frame (`es7210_set_gain` is new).
  A web UI save of the card now sets both at once too.
- The card is written 3 s after the last tap. Its fields are under a
  mutex now (the loop's steps and the web server's save); the key's
  PBKDF2 runs without it, so a 22 s key save never holds the loop.
- Taps ignore a release caused by a second finger landing (the start of
  a two-finger talk) and a finger that travelled (a slow swipe).
- The radio host tests' `random_host.cpp` reads `random_device` on native
  Windows (MinGW has no /dev/urandom: a Reticulum test aborted).
- Checks: radio host tests 183 (OPUS off), gauge tests 77, the preview at
  480 and 720 (two new scenes: the mic gain, muted at 0), `pio run -e p4`,
  `pio run -e s3`.

## 2026-11-07 — The 4"'s quiet mic, and echo cancelling

Asked for: why the mic is very low at its full gain (MIC1 and MIC2 both),
then echo cancelling, since the board has an echo reference.

- Capture is TDM now, as Waveshare's own firmware for the board (the
  factory `SpecAnalyzer` app, the `10_Mic_Record` sketch) reads it: a 64 fs
  bit clock (32-bit stereo slots), four 16-bit slots a frame, MIC1, MIC3 =
  the echo reference (the ES8311's output wired back), MIC2, MIC4. At
  32 fs the ES7210's inputs read about 1 LSB whatever the gain. The radio
  takes MIC1 or MIC2 from it (`mic::unpack_tdm`, host-tested).
- Found with a 1 kHz tone from the unit's own speaker, every PGA code in
  turn: the reference's preamp is linear (707, 2955, 11141 at codes 0, 4,
  8), MIC1's and MIC2's are not: 0 to 37.5 dB gives about +11.5 dB
  (RMS 9, 15, 21, 26, 30, 32, 34 at codes 0-14), the acoustic path itself
  linear (12 dB less speaker, 12 dB less mic). The same on the port unit,
  and with Waveshare's sketch (speech peaks 46 at 0 dB, 49 at 37.5 dB). No
  register changes it. The 4C schematic: the mics' outputs reach MIC1P/
  MIC2P through DC blocks, and MIC1N/MIC2N, behind their own 1 uF blocks,
  go to AGND only through 30 pF: the differential inputs' N legs float,
  so the preamp's gain does not act on the signal. The fix is on the
  board: the 30 pF on the far side of each N leg's 1 uF block shorted to
  AGND (to be done and measured: the gain should then follow the codes as
  the reference's does).
- Echo cancelling and noise suppression (`core/voice.h`, host-tested):
  speexdsp 1.2.1's MDF canceller (120 ms of echo path) against the echo
  reference slot, which is sample-aligned with the mics, then its
  preprocessor (the canceller's residue, and steady noise, 15 dB by
  default). Every frame, talking or not, so it has learned the speaker's
  path when it matters. New card fields, applied live: Echo cancelling,
  Noise suppression (dB), Full duplex (the crew played while talking;
  off by default: the unit stays half duplex, where the canceller takes
  out the key beeps). The Status page's **Radio voice** line: what is on,
  the dB of echo taken out over the last second the speaker played, the
  cost a frame.
- speexdsp vendored unmodified (`src/third_party/speexdsp`, BSD-3-Clause,
  named in the README's credits), compiled through `src/dsp/` wrappers
  with its configuration per file. First run: 10 ms of each 20 ms frame.
  The P4's FPU is single precision and speexdsp's float build calls
  `sqrt`/`exp`/`pow` and uses double constants: every one a soft-float
  call. With the float functions and single-precision constants (in the
  wrapper's config), 5 ms; with -O2 for those five files, 3.4 ms mean,
  4.7 ms at most (on the host the canceller is 130 us, the preprocessor
  95 us).
- The boot-time mic probe (tone and gain sweep) is gone again;
  `es7210_write` with it.
- Checks: radio host tests 191 (the voice tests: 62 dB of echo taken out
  of a synthetic room, a near voice through unchanged, steady noise cut
  18 dB at a 20 dB setting), `pio run -e p4`; both 4" units flashed over
  the air and running (`Radio voice` on both). Not yet heard: the
  canceller against real crew audio, and the mic after the board fix.

## 2026-11-07 — The 4"'s mic after the board fix; the canceller tuned on the units

- Both 4" units modified: the 30 pF on MIC1N/MIC2N replaced by short
  circuits (the 1 uF DC blocks to the ES7210 stay, so the N legs are AC
  grounded and the pins keep their bias).
- A boot-time probe (now removed) measured each unit with its own speaker,
  one unit at a time:
  - Gain: a 1 kHz tone, every PGA code. Linear on both units and both
    mics: MIC1 7 -> 547 (starboard), 8 -> 632 (port) from 0 to 37.5 dB,
    +38 dB; the reference (MIC3) steady at 708/718. The fix works.
  - Echo path: white noise, the reference cross-correlated with the mic.
    The direct path 0.12-0.19 ms (the speaker beside the mic), 88-89 % of
    the response within 5 ms.
  - Canceller tails, speech-band syllables at volume 70: 20, 40 and 60 ms
    alike (ERLE 27-30 dB with the residual suppression, the canceller
    alone 25-26 dB); 80 ms 22-29, 120 ms 24-26, 200 ms 22 (a longer filter
    learns slower). `voice::kTailFrames` 6 -> 2 (40 ms): 3.1 ms a frame
    instead of 3.7. With the noise suppression at 15 dB too: 36-44 dB.
    Volume 100: 26.5-28 dB (the speaker's distortion); volume 50: 21-23
    (the room's noise, RMS 20-30, is the floor of that figure).
  - Double talk, a second voice mixed into the mic: it reaches the crew
    1.3-2.4 dB down (4-7 dB with the noise suppression), the echo beside
    it 15-26 dB down.
  - The preprocessor delays its output by one frame: 20 ms more latency
    whenever the canceller or the noise suppression runs (found when the
    double-talk figure came out as nothing at zero lag).
  - The room's noise, the speaker silent: noise suppression 15 dB takes
    9-15 dB of it out, 30 dB 17-25 dB. A first run, during which something
    in the room made a sound, showed 8 dB: each run now measures the room
    first.
- speexdsp's VAD is off by default; setting it printed a warning on every
  processor's creation, so the call is gone.
- The card offers MIC1 and MIC2 only, on purpose: MIC3 is the echo
  reference (the ES8311's output) and MIC4 is not connected.
- Checks: radio host tests 191, `pio run -e p4`, both 4" units flashed
  over the air with the probe and again without it. Not yet heard: a
  voice against the gain (the cards are at 37.5 dB; the card's default
  is 30).

## 2026-11-07 — The Crew radio card last; the docs and their pictures brought up to date

- Asked for: the gauges and the radio on separate web UI pages ("Gauges",
  "Crew Radio"). SensESP's page list is fixed in its C++
  (`add_routes_handlers`) and its Configuration page shows every card, so
  that is a change to the SensESP fork (the routes and the frontend);
  settled instead on the Crew radio card last on the page: sort order 97
  -> 2000, after the gauges' cards (98-111) and SensESP's WiFi watchdog
  (1000). Both 4" units flashed; `/api/config?cards` lists `/crewradio`
  last.
- `docs/preview-2.1in.png` made again: it had 12 pages, the preview
  renders 14 (depth, wind and battery were missing). New
  `docs/crew-radio-4in.png`: the Crew page (talking, the mic gain, muted,
  a full crew, no network) and the halo over gauge pages, in the README,
  `docs/PAGES.md` and the radio's README. `contact_sheet.py` takes `--ids`
  and `--tile` for it; the commands are in `tools/preview/README.md`.
- The docs said the radio had not run on a board: the README, the
  radio's README (status, phases, first power-up), and AGENTS.md's
  hardware status now say what runs on both 4" units and what is still
  to check. The 4" README has the mic's board fix ("The microphone: a
  board fix") and why the card offers only MIC1 and MIC2.
- Checks: the preview at 480 and 720, looked at; `pio run -e p4`.

## 2026-11-07 — Both mics at once

- Asked for: use MIC1 and MIC2 together, so neither has to be chosen.
  `mic::take_channel` takes `kBoth`: the two averaged (rounded, no
  overflow; host-tested). The card's Microphone is **Both** by default,
  MIC1 or MIC2 kept for a damaged mic. Averaging, not adding: a sum would
  be 6 dB louder, but the unit's own speaker at full volume would clip it
  (about 50 000), and a clipped echo cannot be cancelled.
- Measured on the starboard unit with a boot-time probe (now removed):
  silent, MIC1 19-30, MIC2 20-29, both 16-27 RMS: 0.8-2.2 dB less noise
  (the two mics' noise correlates 0.2-0.6: the room's sound is common to
  both and does not average down, the preamps' hiss does). White noise
  from the speaker: MIC2 hears it one sample (0.06 ms) before MIC1, 3.8 dB
  louder. The 1 kHz tone: both 0.7 dB below the mics' mean. The canceller:
  MIC1 33.3 dB, MIC2 31.4, both 33.3.
- The first version of the probe ran three cancellers in one frame:
  cr-audio (priority 17) kept core 0 busy enough that the task watchdog
  fired 22 s after every boot, too soon for an OTA upload, so the unit
  was flashed over USB. The radio's own path runs one canceller a frame.
- Both units' cards set to Both (they had MIC1, the old default) and
  restarted; the Status page's Radio mic line ends with the mic in use
  (`using both (averaged)`).
- Checks: radio host tests 191, `pio run -e p4`, both 4" units flashed.

## 2026-11-07 — LVGL 9.5 and the P4's PPA tried on the 4": no gain, back on 9.3

- Asked for: would LVGL 9.5 be of use. Nothing in 9.5 itself is (blur
  and shadows, OpenGL, Wayland, 3D, RISC-V vector code the P4 cannot
  run); the candidate was 9.4's draw unit for the P4's PPA (its 2D
  accelerator: solid fills and plain image copies).
- Measured on the starboard 4" with a boot-time bench (now removed): each
  page shown and redrawn whole (fastest of 3), then its value swung over
  30 frames (median), as `tools/preview`'s bench does. Sums over the 7
  pages, full / step: 9.3 582 / 222 ms; 9.5 607 / 235 ms; 9.5 with the
  PPA (fills and RGB565 image copies, `LV_DRAW_BUF_ALIGN` 64) 566 / 228
  ms. The step is the anti-aliased needle and, most of all, the compass
  card turned in software (92-99 ms a step), neither of which the PPA
  draws. Not worth the upgrade, nor the 2.1"'s panel checks it would take.
- What 9.5 needs, for a later try: `lv_image_cache_drop()` is no longer
  reached through `lvgl.h` (`draw_utils.cpp` now includes its header,
  at the same path in 9.3); the PPA unit checks ESP-IDF's Kconfig name
  `CONFIG_LV_DRAW_BUF_ALIGN`, which lv_conf.h must define (64, the P4's
  cache line) in an Arduino build.
- Where a faster 4" would come from: the compass card's turn.

## 2026-11-07 — Faster drawing: what was tried, and what was kept

Asked for: the best possible, novel ways to speed up the drawing. Measured
on the starboard 4" with a boot-time bench (now removed): each page shown
and redrawn whole (fastest of 3), then its value swung over 30 frames
(median step), the flush to the panel and the page's update timed apart.

- Kept, in the drawing code of both boards:
  - The sea horizon invalidates two boxes, round the boat and round the
    heel pointer, at the old and the new attitude, instead of one box over
    the window and the scale: a step 46.1 -> 12.2 ms.
  - A needle invalidates five boxes along its blade and one round the cap
    instead of its bounding box (about half the pixels on a slanted
    needle): the coolant and oil dials 14.5 -> 12.2 ms.
  - The LCD's panel (gradient, rounded corners, border) is drawn into the
    dial face once (`LcdReadout::move_background_into()`); the readout
    draws only its texts. The gradient is now dithered with the face:
    smoother than LVGL's own, which banded. 2.1" renders: 50 pixels
    differ, by at most 11 levels.
  - 4": the LCD's digits (63 px, above the largest font) were a text image
    scaled at every redraw; now each character is scaled once, the first
    time it is needed, and the text is laid out from them in the same
    tabular cells (`lcd_readout.cpp`). The rpm dials, whose readout changes
    every step: 26.7 -> 16.8 ms. Scaling the whole text once per change
    instead (tried first) cost 10 ms a change and was slower.
  - The halo's glyph images and the LCDs' characters share one setting for
    where small images go (`set_glyph_handlers()`, PSRAM on the 4").
- All pages summed, 4": steps 222 -> 157 ms, full redraws 582 -> 562 ms.
  The compass is still 87 ms a step (it was 92).
- Tried and dropped:
  - Sprites: the compass card and the needles rendered once into images
    and turned each step by a rotor of my own (16.16 fixed point,
    bilinear, empty blocks skipped through a summed-area table of the
    sprite, the rows split with a helper task on core 0), into a
    screen-sized buffer LVGL blended unrotated. Pixel-for-pixel close to
    the vector drawing in the preview, but slower on the board: the card's
    turn alone took 50 ms, a needle's 10-13 ms, and the blend of the
    buffer 45 ms on a full redraw.
  - A 12-line draw buffer, in internal RAM (full redraws 568 -> 436 ms,
    steps 185 -> 168 ms, but the radio's memory guard turned Reticulum off
    twice) and in PSRAM (no gain).
- Why: the 4"'s drawing is bound by memory. A PSRAM to PSRAM copy runs at
  38-42 MB/s with 96 KB blocks and 350 MB/s with 16 KB ones (the L2 cache
  is 128 KB); PSRAM to internal 160-274 MB/s, internal to PSRAM 250-352
  MB/s; the CPU at 400 MHz. A sprite adds memory passes; less to redraw
  takes them away. Even with nothing moving drawn, a compass step costs
  about 40 ms (LVGL's pipeline over 284 000 pixels and the flush, 9 ms);
  its marks and rotated labels about 46 ms more.
- Left for later, if the compass matters: a custom LVGL draw unit that
  turns an A8 card straight into each chunk of the draw buffer (a working
  set the cache holds), or labels drawn without a transform.
- Checks: gauge host tests 77, the preview at 480 and 720 (the 720 pages
  differ from before only inside the LCD panels: no needle left behind
  by the smaller invalid areas), `pio run -e p4`, `pio run -e s3`, both 4"
  units flashed; both 2.1" units flashed and their panels looked at: no
  flicker or shift.

## 2026-11-07 — The docs on main checked against the code

After the crew branch was merged (#13), every document was read against
what is in the tree. Fixed:

- The radio's README: its layout leaves nothing out now (`src/dsp/`,
  `src/third_party/` with speexdsp, the mic's TDM reading and the echo
  canceller in `src/core`, the ES7210 and Reticulum in `src/esp`); the
  tests table has `test_voice.cpp` and `test_lifecycle.cpp`, the relayed
  vector and the averaged mics; the screen is no longer called mock-ups.
- The 4" README's hardware table: the mic is read in TDM with MIC3 as the
  echo reference, and needs the board fix.
- ADDING_GAUGES.md, a new page style: the LCD's panel into the face, exact
  invalid areas (the needle's strips, a box per moving part), a ghost in
  the preview as the sign of one too small, and the 4"'s drawing being
  bound by PSRAM too.
- README and AGENTS.md: CI runs the Crew Radio's host tests too.
- CodeRabbit's review of #14: the preview's images test movement only
  where a page starts away from its sample (a dial's needle; a compass
  card starts at its heading), so a new style's moving part is checked
  moving too; and the station's talk-disc screen (phase 3b) is preview
  only, the firmware's Crew page has it off (the radio's README said a 4"
  with only the Crew page was the station).

## 2026-11-08 — A quieter serial log

All four units ran 13 h after the last OTA (2026-11-07) without a restart:
the Signal K server gone for 2 min at 03:07 (all four reconnected by
03:09), the radio's memory guard taking Reticulum off for a second (once
on the starboard 4", 7 times on the port one), one lost reply from the
port 4"'s C6 (a 5 s pass, as designed). The serial logs, over all their
boots, were mostly noise:

| Lines (4" starboard log) | What |
|---|---|
| 120 401 | SensESP's `OTA Progress: N%`, one per chunk: 3300 an upload |
| 7 618 | `Dropping oldest received value update (queue full)`, nearly all during uploads (the upload holds the loop) |
| 4 390 + 4 130 | the `loop` and `heap` lines, once a minute: 86 % of a running unit's log |
| 1 042 | `saveable.cpp: Not loading configuration: no config_path specified`, 15 a boot |

and on the port 4", 177 `LVGL held the loop for 150-250 ms` in 13 h.
Changed in `gauge_app.cpp`, without touching SensESP: the upload's
progress every 10 % (our own `onProgress`, which replaces SensESP's as
`onStart` already did); SensESP's WebSocket client at errors only once an
upload has started (the unit restarts after it, either way); `saveable.cpp`
at warnings; the `heap` and `loop` report every 10 minutes, the `loop`
line with how many passes of each side stalled; a stall logged only when
it is its side's first since the report, or 1 s or more. Nothing is sent
to the server (no system-info outputs), so it never had a flood to take.
Left as they are: the memory guard's lines, the reconnects, the GNSS clock
line (16 in 13 h on the port 4", in pairs seconds apart: a matter for the
clock's rule, not the log).

## 2026-11-08 — The talker sees the key at once

Two fingers on the 4": the crew's screens showed the talker before the
talker's own did. The talk key's state reached the status only in
cr-audio's once-a-second copy (0-1 s late), and the halo and the Crew
page read the view every 250 ms; a receiver's cr-net publishes as soon as
the number of talkers changes, so the crew had it sooner. Now cr-audio
writes the key's state to an atomic every 20 ms frame, which `status()`
reads (as it reads the volume), and the halo and the Crew page look every
50 ms (a copy of the status; they redraw only on a change). What is left
between the fingers and the screen: the 20 ms touch sample and the key's
40 ms debounce, and on the 4" one LVGL pass (about 100 ms) for the
redraw, during which the touch is not sampled either.

## 2026-11-08 — Two talkers at once: the radio under load

Two crew talking at the same time dropped every client (the plugin, the
phones, the 4" units) off the Reticulum hub, and the starboard 4" restarted
twice (20:59, 21:06): the task watchdog, CPU 0 without a moment of idle for
5 s, cr-audio in the echo canceller when it fired. Each time the memory
guard had just shed Reticulum and cr-rns never answered ("paused" missing
from the log): it was stuck in its loop of blocking sends. Two research
passes, over crew-radio's app and plugin and over RNS 1.6.0's source:

- each 20 ms frame is sealed and written separately on every Reticulum
  link (TCP_NODELAY, one segment, one ESP-Hosted DMA buffer each);
- every node on the boat's WLAN that is also on Reticulum bridged every
  frame again, and the boat's nodes were linked to each other through the
  hub too: about ten copies of a frame through the hub for one talker;
- rnsd's TCPServerInterface forwards from one thread with a blocking
  `sendall`: one client that stops reading stalls it for all, its queue of
  1024 overflows in about 2 s and the keep-alives go with it;
- the plugin's and the app's backlog rules then redialled, which drops
  every link of that node.

One rule set for all three clients (crew-radio branch `radio-under-load`,
this repo the same): one forwarder per boat chosen from the hellos' new
bits, split horizon on Reticulum, voice dropped rather than sent late and
no redial on a backlog, and channel busy (listen before talk, the higher
id gives way in a collision, an override after 1 s held). Here:
`core/forwarder.*`, `core/channel_floor.*`, link tags through
`ReticulumNode`, a cr-rns that never waits on its socket, the busy cue,
and "CHANNEL BUSY" on the halo and the Crew page. The hub: BackboneInterface
(already in rnsd 1.5.4, egress control included).
