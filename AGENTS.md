# AGENTS.md — working rules for this project

> The working contract for the repository, modelled on
> [drive-remote-controller](https://github.com/KEGustafsson/drive-remote-controller)'s.
> The SensESP idioms are in *SensESP v3 API rules*; everything else is what
> keeps gauge displays on a boat trustworthy.

## What this is

Signal K instrument gauges (classic steel style) for Waveshare round touch
displays. Two boards (4" and 2.1"), two firmware projects, one shared
library. Each board has one env and one firmware for all of its units.
What a unit is, is set in its web UI's Unit card and saved on it
(`unit_card.*`, rules in `unit_identity.h`): its role (Starboard, Port or
General), which picks the factory pages and its `secrets.h` address; its
hostname; its display name. Nothing about a unit is fixed in code (only
the firmware version is, `VERSION`). Never add a per-unit or per-upload
env or `-D` flag: each such env compiles everything again.

| Device | Project | env | Upload |
|---|---|---|---|
| 4" | `waveshare-esp32-p4-wifi6-touch-lcd-4c/` | `p4` | `--upload-port COMn` (USB), `port` / `stbd` / an address (OTA) |
| 2.1" | `waveshare-esp32-s3-touch-lcd-2.1/` | `s3` | the same |

The gauges only *display*: nothing here commands a machine. The rules below
are about never showing a wrong number with confidence.

## Non-negotiables

1. **Never present unconfirmable data as live.** A value older than the
   page's data timeout (default `kStaleAfterMs`) shows dashes and parks the needle; a non-numeric delta
   (`null` from a source that lost its data) is dropped, never coerced to 0.
   `sk_listeners.h` (with `sk_parse.h`) exists for exactly this -- do not swap
   it for the stock `SKValueListener<float>`. The rules live in `sk_parse.h`
   and `value_gate.h` and are unit-tested (`tests/`); keep them there.
2. **Signal K retains the last value of a path forever.** A value that stops
   updating is not proof the source is alive; staleness is judged on arrival
   time (`GaugeValue::updated_ms`), never on the value.
3. **Recover, don't freeze.** An unattended gauge must reboot rather than hang
   on a stale picture: display init failure restarts, the LVGL assert handler
   restarts on ESP targets, the loop runs under a 12 s watchdog, and a loop
   that keeps stalling 4 s or more (three times a minute) restarts
   (`gauge_app.cpp`). Keep the watchdog longer than any single call the loop
   may legitimately wait on (the 4"'s WiFi requests: 5 s). A link that stops
   working restarts the unit too (`link_watch.cpp`, rules in
   `link_policy.h`): 3 min without the network (SensESP's WiFi watchdog),
   5 min without Signal K while WiFi is up, and on the 4" 60 s without a
   heartbeat from the C6. These restarts use `esp_restart_noos()`, not
   `esp_restart()`, whose shutdown steps wait on the C6.
4. **Never commit a credential.** `secrets.h` is gitignored anywhere in the
   tree; the committed `secrets.example.h` keeps `SECRET_OTA_PASSWORD`,
   `SECRET_ADMIN_PASSWORD` and `SECRET_AP_PASSWORD` empty so a build without
   secrets.h never accepts a published password. CI checks all of it.

If a task would require breaking one of these, stop and flag it.

## Where code goes

- **`common/src/`** -- the shared PlatformIO library (`SignalKGauges`), linked
  into both projects with `symlink://../common`.
  - Pages are user-defined: a "Pages" card (`PageCount`, how many to swipe
    through, up to `kMaxPages` = 12) and a card per page with its style
    (dial, compass, sea horizon), title, path, unit, scale, decimals and
    timeout, like a SKip gauge widget.
  - `gauge_defs.cpp` holds the factory pages (by side) and the style
    templates: SK paths, display units, ranges, ticks. Never hard-code a path
    or range anywhere else. Zones are not in it: dials draw the server's
    `meta.zones`.
  - `gauge_settings.*` -- the page cards (`GaugeSettings`, `/Pages/<n>`) and
    `GaugeConfig`, which holds the boot snapshot of the settings, the
    metadata received and the resolved `GaugeDef` each page is drawn from.
  - Host-safe logic, unit-tested in `tests/` (no Arduino, no SensESP):
    `page_rules.*` (what a card holds, the rules a save follows, the card's
    schema), `gauge_resolve.*` and `units.*` (settings + metadata ->
    `GaugeDef`, also used by the preview), `value_gate.*` (replay window,
    `null` grace, staleness), `sk_parse.h` (only a JSON number is a
    reading) and `card_store.*` (a card's file replaced all or nothing; at
    boot, a save a power cut interrupted before it was done is undone, and
    one that was done is kept), `link_policy.h` (when a dead link restarts
    the unit). Put new logic of this kind there, with a test, rather than in
    the Arduino glue.
  - `ui/` -- pure LVGL drawing. No Arduino, no SensESP: it must keep compiling
    on the host for `tools/preview`, which CI runs.
  - `sk_data.*`, `sk_listeners.h`, `status_led.h`, `gauge_app.*`, `gauge_ui.*`,
    `link_watch.*`, `lvgl_port.*` -- SensESP / Arduino / ESP32_Display_Panel
    glue.
- **`waveshare-*/`** -- one PlatformIO project per board: `platformio.ini`
  (one env for both sides), `src/main.cpp` (board bring-up only), `include/lv_conf.h`,
  and for the 4" `include/esp_panel_board_custom_conf.h`.
- **`include/secrets.example.h`** -- template for the gitignored, shared
  `include/secrets.h`: WiFi, Signal K server, OTA password, device addresses.
- **`VERSION`** -- the firmware's version (one line, e.g. `0.1.0`), shown in
  the boot log, on the info overlay and on the web UI's Status page ("Gauge
  firmware"). `scripts/gauge_version.py` (pre:) writes it into the
  gitignored `common/src/gauge_version_value.h`, which only
  `gauge_version.cpp` includes: a new version recompiles one file. Bump it
  for a release, never as a `-D` flag.
- **`scripts/ota_auth.py`** -- injects the OTA password and address from
  secrets.h at upload time. Never write `--auth=` into a platformio.ini.
  `scripts/ota_quiet.py` (post:) drops the builder's espota `--debug`, which
  would print the password into the upload log.
- **`tools/preview/`** -- renders every page on the host from `common/src/ui`.
- **`tools/copy-pages/`** -- copies one unit's page cards to another over the
  web API (with `--replace` for the other side's paths), carrying the
  target's `_rev`; the login from secrets.h.
- **`tests/`** -- host unit tests (`tests/README.md`).
- **`docs/`** -- `PAGES.md` (end-user guide to the pages), `BRINGUP.md`
  (first power-up of a device), `ADDING_GAUGES.md` (another page, another
  unit, a new page style, a new board: what to change), `JOURNAL.md`.

## Commands

```bash
cd waveshare-esp32-p4-wifi6-touch-lcd-4c && pio run -e p4                  # build the 4"
cd waveshare-esp32-s3-touch-lcd-2.1 && pio run -e s3 -t upload --upload-port COM4  # USB
cd waveshare-esp32-s3-touch-lcd-2.1 && pio run -e s3 -t upload --upload-port port  # OTA
cmake -S tools/preview -B build/preview && cmake --build build/preview -j
mkdir -p out && build/preview/preview 480 && build/preview/preview 720
cmake -S tests -B build/tests && cmake --build build/tests -j && build/tests/gauge_tests
```

Each board has one env, so building it builds every unit's firmware.
CI (`.github/workflows/ci.yml`) runs the host unit tests,
renders the preview at both sizes, and checks the scripts and credential
rules on every push to `main` and every pull request. The firmware builds
(`p4`, `s3`) are too heavy for every change: they run only when the
workflow is started by hand, so build a firmware change locally, both
boards. Run the relevant check locally
anyway: CI is the backstop, not the first place to find out.

A UI change is not done until the preview has been rendered and looked at.
A logic change in the host-safe files is not done until `tests/` covers it.

## SensESP v3 API rules

- SensESP comes from the maintainer's fork, `KEGustafsson/SensESP`, branch
  `fix/ws-message-reassembly`: SignalK/SensESP main plus the fixes the gauges
  need that are not upstream yet (received WebSocket messages joined before
  parsing). A SensESP fix goes there, with its test in SensESP's
  `test/native`, not as a patch in this repo; back to SignalK/SensESP once
  merged. Unpinned, as in drive-remote-controller: record the resolved
  revision from the build log when a build matters.
- **Event loop:** SensESP owns its `reactesp::EventLoop`. Call
  `event_loop()->tick()` and `lv_timer_handler()` from the same Arduino
  `loop()` (`gauge_app_loop()`); listener callbacks run there too, which is why
  the UI reads `SkData` without locks. Do not create a second ReactESP or move
  LVGL to another task without adding locking.
- Use `SensESPAppBuilder`. `set_system_status_led()` must stay set to
  `ScreenStatusLed`: the ESP32-S3 Arduino variant's `PIN_RGB_LED 48` is an LCD
  data line on the 2.1" board.
- Page settings are a `FileSystemSaveable` (`GaugeSettings`, `/Pages/<n>`)
  with its own `ConfigSchema`; listeners are not `ConfigItem`s. Listeners
  derive from `GaugeListener` (`sk_listeners.h`); each page also has a
  `sensesp::SKMetadataListener` on its path that hands metadata to
  `GaugeConfig::set_meta()`. SensESP gives values only to listeners whose
  `wants_meta()` is false: never make a value listener want metadata (it
  then receives none -- seen on the first hardware run).
- Logging is `ESP_LOGI/W/E(tag, ...)`. The one exception is LVGL's own log
  callback in `lvgl_port.cpp`, which prints LVGL's preformatted lines.
- If unsure of an API, read the fetched source under
  `<project>/.pio/libdeps/<env>/` (present after any `pio run`). Do not invent
  function names.

## Gotchas

- **LVGL 9.3** draw API (`lv_draw_*` on an `lv_layer_t`), not v8. Static dial
  faces are drawn once into an XRGB8888 canvas and dithered to RGB565
  (`finish_canvas`); only the moving parts redraw.
- Dial angles are degrees clockwise from 12 o'clock; LVGL arcs start at
  3 o'clock -- use `to_lv_arc_angle()`.
- 4" touch: GT911 at 0x5D, or 0x14 on some boards. 4" backlight is driven
  inverted (`ON_LEVEL 0`).
- ESP32-P4 silicon: `board = esp32-p4_r3` (v3.x); pre-v3 needs `esp32-p4`.
- 4" WiFi is an ESP32-C6 over SDIO (ESP-Hosted). Its buffers come from
  internal DMA-capable RAM while data flows; when that runs out the link
  stalls for good and the loop watchdog restarts the unit. Keep large
  buffers (LVGL's included) in PSRAM on the 4"; the boot log's `heap` line
  shows what is left. The C6 needs ESP-Hosted 2.12.13, the version of
  Arduino-ESP32 3.3.12 (`tools/c6-update`; 4" README).
- 4" WiFi calls block: `WiFi.SSID()`, `WiFi.RSSI()` and the like are
  requests to the C6, answered within ESP-Hosted's 5 s
  (`DEFAULT_RPC_RSP_TIMEOUT`) or given up. SensESP makes them from the loop.
  Do not add such calls to the loop, and keep the loop watchdog above 5 s
  (`kLoopWatchdogMs`): at 5 s one lost reply rebooted the unit.
- 2.1" RGB panel: it reads its frame buffer from PSRAM all the time. It
  needs the 12 MHz pixel clock and the 20-line bounce buffer set in
  `board_start.cpp` (less flickers and drifts), and the bounce buffer's
  internal RAM is paid for with a 20-line LVGL draw buffer: SensESP needs
  an 8 KB contiguous block for every connection attempt, or the unit never
  reconnects (`connect worker spawn failed`). Check the `heap` line's
  largest block after any internal-RAM change.
- 2.1" PSRAM bandwidth is the panel's: its bounce-buffer interrupt must
  read PSRAM in time, over the bus the S3 shares between PSRAM and flash.
  A missed read shifts the picture by one bounce buffer (20 lines) until a
  reboot. Flash access stops that interrupt and PSRAM outright (a file
  read every 2 s shifted it within half a minute); memory-heavy per-frame
  drawing (a 1 MB lookup per compass step) and code compiled for speed
  (-O2: more flash fetches) did it too. Make drawing cheaper in CPU, not in
  memory traffic, and look at the panel after any drawing or build-flag
  change.
- 2.1" framework: its `platformio.ini` rebuilds the Arduino libraries with
  `custom_sdkconfig`: `CONFIG_SPIRAM_XIP_FROM_PSRAM` and
  `CONFIG_LCD_RGB_ISR_IRAM_SAFE` so flash access no longer stops the panel,
  and `CONFIG_LCD_RGB_RESTART_IN_VSYNC=n`: with it on (Arduino's choice)
  ESP-IDF's RGB driver never resets the counter that picks which bounce
  buffer to refill, so one late interrupt shifts the picture 20 lines for
  good (board README). The project has its own `core_dir`
  (`~/.pio21`, short: a longer path made a Windows command line too
  long): the rebuild replaces the framework in place, and in
  a shared `~/.platformio` the two boards' builds undid each other. The first build of each env, and any change to those lines,
  compiles ESP-IDF (63 min the first time on the first PC). On Windows run
  it from PowerShell or cmd: from Git Bash ESP-IDF's tool installer
  refuses MSYS, CMake is never installed and the build fails with
  `FileNotFoundError` in `run_cmake`. Running the code
  from PSRAM costs about 1.4 MB of it. Verified with all three: no flicker
  or shift, also while the web UI's configuration pages are browsed. When
  changing these lines, delete the project's `sdkconfig.<env>` first, or
  ESP-IDF keeps its old values. PlatformIO must be pioarduino 6.2.0, in
  the `pio` you run and in `~/.pio21/penv`: the platform (55.03.312)
  refuses 6.2.0.post1, the newest (board README). Both boards are on
  Arduino-ESP32 3.3.12 since 2026-11-03; the first 2.1" build on it took
  1 h 28 min.
- PlatformIO's build cache keys each object on its full command line: a
  new global `-D` flag recompiles everything. Pass a per-file setting in
  code instead (the draw-buffer sizes are in each `src/main.cpp`). The
  command line includes the env's own library folder (`.pio/libdeps/<env>`),
  so the cache never carries over between envs: that is why each board has
  one env, with the side read on the unit and the upload method chosen by
  the upload port (until 2026-11-03 there were four envs per board, and a
  port unit's build took as long as a fresh one).
- **Never `pio pkg update`** in these projects: it rewrites
  `platformio.ini`, dropping every comment and adding a `lib_deps` with
  only the updated library to the env, which then builds without the rest
  (`esp_display_panel.hpp: No such file or directory`, 2026-11-03). To
  take a new commit of SensESP (a git branch, which a build never fetches
  by itself), delete `.pio/libdeps/<env>/SensESP` and build: the build
  installs the branch's latest. Check with
  `git -C .pio/libdeps/<env>/SensESP log --oneline -1`.
- Any edit of a `platformio.ini`, a comment included, deletes that
  project's `.pio/build/<env>` (PlatformIO hashes the whole config into
  `project.checksum`): every object is copied back from the build cache,
  about 2 min on the 4". Batch such edits. `scripts/fast_scons.py` (pre:)
  makes SCons trust unchanged timestamps and reuse its header scan: a
  4" build with nothing to compile went from 139 s to 66 s, most of the
  rest being PlatformIO's library scan (`Scanning dependencies`, 45 s).

## Hardware status

**4" and 2.1" starboard: brought up** (2026-10-31 / 11-01,
`docs/JOURNAL.md`): boot, touch, WiFi, Signal K, live readings with the
server's zones on all six pages; OTA updates of both (2026-11-01).
Readings against another instrument and stale/recovery behaviour on
hardware are not verified yet. Follow `docs/BRINGUP.md` and the first power-up
checklists in each board folder's README before trusting a reading.

## Journal

Keep `docs/JOURNAL.md` up to date: an entry for each meaningful piece of work,
and read it at session start to resume correctly.
