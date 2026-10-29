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
  `EEEE`, distinct from no-data dashes.
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
  now below 16 MB.

Verified: both boards build without warnings from this code; the preview
renders every page; stale/overflow states rendered and inspected; incremental
redraws are pixel-identical to full redraws on every page; the CI credential
check passes the clean tree and fails each injected credential.
