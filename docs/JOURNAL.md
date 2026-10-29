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
