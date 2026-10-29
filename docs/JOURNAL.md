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

Verified: all eight envs compile; every page renders on the host at 480 and
720 px (`tools/preview`). Not verified: anything on hardware -- no unit has
been flashed yet.

CodeRabbit's first review led to: an empty OTA password in the committed
example, `$`/whitespace handling in `ota_auth.py`, restart on display-init
failure, reboot on LVGL asserts, a bounded idle delay in the loop, and
preview-tool robustness. CI added (`.github/workflows/ci.yml`): all four unit
builds, the host preview, and the credential checks.
