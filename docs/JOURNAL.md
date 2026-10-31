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
