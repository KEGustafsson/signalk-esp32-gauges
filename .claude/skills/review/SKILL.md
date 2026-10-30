---
name: review
description: >
  Review gauge firmware code for correctness, hardware compatibility, and
  SensESP/LVGL best practices. Use when user says "review", "check my code",
  or before finalizing a change.
argument-hint: "[optional: p4, s3, common, ui, preview, or a specific concern]"
---

# Gauge Firmware Code Review

Review the firmware for correctness, hardware compatibility, and SensESP / LVGL
best practices.

## Phase 0: Scope

- Identify what to review: the shared library (`common/src/`), its UI
  (`common/src/ui/`), one board project (`waveshare-*/`), the scripts, or
  `tools/preview/`. Ask if it was not given.
- Read [AGENTS.md](../../../AGENTS.md) first: its non-negotiables look like
  removable complexity and are not.
- Read the board folder's README and `platformio.ini` for the build
  configuration, and `common/src/gauge_defs.cpp` for what each page shows.

## Phase 1: Review Perspectives

Only report genuine issues -- don't flag things that aren't problems.

### Correctness
- Logic errors, off-by-one, uninitialised variables, unit conversions (SK is
  SI: rad, m/s, K, Hz)
- Stale or non-numeric data shown as live (AGENTS.md non-negotiable 1)
- Angle wrap-around (heading 359 -> 0), clamping at the dial ends
- LVGL object lifetime: everything a page creates must die with its root
- Memory: canvases are full-screen; only one page may be alive at a time

### Hardware Compatibility
- Pins match the board README and the ESP32_Display_Panel board config
- No pin conflicts (GPIO48 is an LCD data line on the 2.1")
- I2C addresses don't collide (2.1": TCA9554 0x20, CST820, QMI8658, PCF85063)
- Partition table fits the flash size and leaves an OTA slot
- Build flags correct for the target MCU (OPI PSRAM on S3, P4 silicon rev)

### SensESP Patterns
- `SensESPAppBuilder`, `event_loop()->tick()`, one loop task for SensESP+LVGL
- Signal K path defaults only in `gauge_defs.cpp`, editable per page via the
  `GaugeSettings` card; zones from the server's `meta.zones`, never hard-coded
- No hard-coded WiFi credentials; secrets only in the gitignored secrets.h

### Power and Performance
- No busy loops; redraw only invalidated areas
- Subscription periods (`listen_delay_ms`) reasonable for each gauge
- Internal RAM left for WiFi (LVGL buffers fall back to PSRAM)

## Phase 2: Synthesize

- **Must fix**: crashes, a frozen or wrong reading presented as valid, a
  committed credential, anything breaking an AGENTS.md non-negotiable.
- **Should fix**: works but incorrectly, or will fail eventually.
- **Suggestions**: style, minor optimisations.

Explain each issue plainly: what is wrong, why it matters, what the fix is.

## Phase 3: Fix

Offer to fix all "must fix" and "should fix" issues. Apply fixes, re-read the
code, then verify before claiming anything is fixed:
- `pio run -e <one env per affected board>`
- for any `common/src/ui` change, render `tools/preview` at 480 and 720 and
  look at the images.

Suggest next steps without overstating what a green build means: a gauge that
compiles has not been shown to read the right value on the boat.
