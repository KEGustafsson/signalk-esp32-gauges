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
