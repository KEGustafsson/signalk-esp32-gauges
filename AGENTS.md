# AGENTS.md — working rules for this project

> The working contract for the repository, modelled on
> [drive-remote-controller](https://github.com/KEGustafsson/drive-remote-controller)'s.
> The SensESP idioms are in *SensESP v3 API rules*; everything else is what
> keeps gauge displays on a boat trustworthy.

## What this is

Signal K instrument gauges (classic steel style) for Waveshare round touch
displays. Two boards (4" and 2.1"), each built as a starboard or a port
device (the side only picks the engine of the factory pages), two firmware
projects, one shared library:

| Device | Side | Project | env (OTA / USB) |
|---|---|---|---|
| 4" | starboard | `waveshare-esp32-p4-wifi6-touch-lcd-4c/` | `p4_stbd` / `p4_stbd_usb` |
| 4" | port | `waveshare-esp32-p4-wifi6-touch-lcd-4c/` | `p4_port` / `p4_port_usb` |
| 2.1" | starboard | `waveshare-esp32-s3-touch-lcd-2.1/` | `s3_stbd` / `s3_stbd_usb` |
| 2.1" | port | `waveshare-esp32-s3-touch-lcd-2.1/` | `s3_port` / `s3_port_usb` |

The gauges only *display*: nothing here commands a machine. The rules below
are about never showing a wrong number with confidence.

## Non-negotiables

1. **Never present unconfirmable data as live.** A value older than the
   page's data timeout (default `kStaleAfterMs`) shows dashes and parks the needle; a non-numeric delta
   (`null` from a source that lost its data) is dropped, never coerced to 0.
   `sk_listeners.h` exists for exactly this -- do not swap it for the stock
   `SKValueListener<float>`.
2. **Signal K retains the last value of a path forever.** A value that stops
   updating is not proof the source is alive; staleness is judged on arrival
   time (`GaugeValue::updated_ms`), never on the value.
3. **Recover, don't freeze.** An unattended gauge must reboot rather than hang
   on a stale picture: display init failure restarts, the LVGL assert handler
   restarts on ESP targets.
4. **Never commit a credential.** `secrets.h` is gitignored anywhere in the
   tree; the committed `secrets.example.h` keeps `SECRET_OTA_PASSWORD`,
   `SECRET_ADMIN_PASSWORD` and `SECRET_AP_PASSWORD` empty so a build without
   secrets.h never accepts a published password. CI checks all of it.

If a task would require breaking one of these, stop and flag it.

## Where code goes

- **`common/src/`** -- the shared PlatformIO library (`SignalKGauges`), linked
  into both projects with `symlink://../common`.
  - Pages are user-defined: `kMaxPages` (12) web UI cards, each Off or a
    style (dial, compass, sea horizon) with title, path, unit, scale,
    decimals and timeout, like a SKip gauge widget.
  - `gauge_defs.cpp` holds the factory pages (by side) and the style
    templates: SK paths, display units, ranges, ticks. Never hard-code a path
    or range anywhere else. Zones are not in it: dials draw the server's
    `meta.zones`.
  - `gauge_settings.*` -- the page cards (`GaugeSettings`, `/Pages/<n>`) and
    `GaugeConfig`, which holds the boot snapshot of the settings, the
    metadata received and the resolved `GaugeDef` each page is drawn from.
    `gauge_resolve.*` and `units.*` (host-safe, used by the preview) turn
    defaults + settings + metadata into that `GaugeDef`.
  - `ui/` -- pure LVGL drawing. No Arduino, no SensESP: it must keep compiling
    on the host for `tools/preview`, which CI runs.
  - `sk_data.*`, `sk_listeners.h`, `status_led.h`, `gauge_app.*`, `gauge_ui.*`,
    `lvgl_port.*` -- SensESP / Arduino / ESP32_Display_Panel glue.
- **`waveshare-*/`** -- one PlatformIO project per board: `platformio.ini`
  (one env per device and side), `src/main.cpp` (board bring-up only), `include/lv_conf.h`,
  and for the 4" `include/esp_panel_board_custom_conf.h`.
- **`include/secrets.example.h`** -- template for the gitignored, shared
  `include/secrets.h`: WiFi, Signal K server, OTA password, device addresses.
- **`scripts/ota_auth.py`** -- injects the OTA password and address from
  secrets.h at upload time. Never write `--auth=` into a platformio.ini.
  `scripts/ota_quiet.py` (post:) drops the builder's espota `--debug`, which
  would print the password into the upload log.
- **`tools/preview/`** -- renders every page on the host from `common/src/ui`.

## Commands

```bash
cd waveshare-esp32-p4-wifi6-touch-lcd-4c && pio run -e p4_stbd     # build one unit
cd waveshare-esp32-s3-touch-lcd-2.1 && pio run -e s3_stbd_usb -t upload  # first flash
cd waveshare-esp32-s3-touch-lcd-2.1 && pio run -e s3_stbd -t upload      # OTA
cmake -S tools/preview -B build/preview && cmake --build build/preview -j
mkdir -p out && build/preview/preview 480 && build/preview/preview 720
```

Building one env per board is enough to prove the code compiles: the port and
starboard envs differ only in `-D` flags, and the `_usb` envs only in the
upload method. CI (`.github/workflows/ci.yml`) builds the starboard unit of
each board (`p4_stbd`, `s3_stbd`), renders
the preview at both sizes, and checks the scripts and credential rules on every
push to `main` and every pull request. Run the relevant check locally anyway:
CI is the backstop, not the first place to find out.

A UI change is not done until the preview has been rendered and looked at.

## SensESP v3 API rules

- SensESP comes from its git main branch (unpinned, as in drive-remote-
  controller). Record the resolved revision from the build log when a build
  matters.
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
  derive from `GaugeListener` (`sk_listeners.h`), which opts in to metadata
  (`wants_meta()`) and hands it to `GaugeConfig::set_meta()`.
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

## Hardware status

**No device has been flashed yet.** Everything is build- and
host-verified only. Work through the first power-up checklists in each board
folder's README before trusting a reading.

## Journal

Keep `docs/JOURNAL.md` up to date: an entry for each meaningful piece of work,
and read it at session start to resume correctly.
