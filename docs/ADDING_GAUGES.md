# Adding gauges

"More gauges" can mean four different jobs. The first two need no code and
no new build; the last two change the firmware.

| You want | What it takes | Section |
|---|---|---|
| Another page (another reading) on a unit | Web UI only | [1](#1-another-page-on-a-unit) |
| Another physical display of a board you already have | Flash the existing build, set its side and pages | [2](#2-another-unit-of-an-existing-board) |
| A new kind of page (not a dial, compass or sea horizon) | Code in `common/src`, tests, preview | [3](#3-a-new-page-style) |
| A display board this project does not support yet | A new board project | [4](#4-a-new-board) |

Changing the six factory pages a fresh unit starts with is in
[5](#5-changing-the-factory-pages).

## 1. Another page on a unit

Up to 12 pages per unit, set up in the unit's web UI, no build:

1. Open the unit's web UI (`http://gauge-21-port.local`, or its IP from the
   info overlay: swipe down on the gauge). Log in with `SECRET_ADMIN_USER` /
   `SECRET_ADMIN_PASSWORD` from `secrets.h`.
2. **Configuration → Pages**: raise **Number of pages** (e.g. 6 → 7), save.
3. Restart when asked. The new page appears as **Page 7**, a dial with no
   path (dashes on the gauge).
4. In the **Page 7** card set the style, title, Signal K path (the server's
   Data Browser lists every path), display unit, decimals, data timeout;
   save and restart.

Zones and the scale come from the Signal K server's metadata for the path
(`meta.zones`, `meta.displayScale`): set them on the server, not on the
unit. Every setting is described in [PAGES.md](PAGES.md), with examples.

## 2. Another unit of an existing board

Each board has one firmware for all its units, so a new 4" or 2.1" display
needs no new build: flash the firmware already built, then tell the unit
which it is.

1. **4" only: update the WiFi chip's firmware first** (ESP32-C6, once per
   board): [4" README](../waveshare-esp32-p4-wifi6-touch-lcd-4c/README.md#update-the-wifi-chips-firmware-once-per-board).
2. **Flash over USB** from the board's folder (`pio device list` shows the
   port). If the board's env is built, nothing compiles:

   ```bash
   cd waveshare-esp32-s3-touch-lcd-2.1
   pio run -e s3 -t upload --upload-port COM9
   ```

   It joins WiFi from `secrets.h` and comes up as the board's starboard unit
   (`gauge-21-stbd`).
3. **Its side**: web UI, **Configuration → Unit** → **Port** (or
   Starboard), save, restart. The side picks the factory pages' engine, the
   name (`2.1in PORT`) and the network name (`gauge-21-port`).
4. **Approve it in Signal K**: server admin UI, **Security → Access
   Requests**, the request named after the unit; read access is enough.
5. **Its pages**: as in section 1. To copy another unit's pages, set the
   same fields card by card, or through the web API: `GET` then `PUT`
   `http://<unit>/api/config/Pages/<n>` (digest login as above) with the
   other unit's card, but keep the target's own `_rev` value in it, or
   the save is refused as out of date.
6. **`secrets.h`**: `SECRET_OTA_HOST_<ENV>_STBD` / `_PORT` hold each side's
   address. They are used for over-the-air updates by side
   (`pio run -e s3 -t upload --upload-port port`) and, with
   `SECRET_USE_STATIC_IP 1`, as the unit's static IP.

After that, updates go over WiFi: `--upload-port port` or `stbd`, or the
unit's address.

**A third unit of the same board** (say a helm display): a unit's identity is
its hostname, and only `-port` makes it a port unit. Give it a name of its
own in the web UI, **Configuration → `/system/hostname`** (e.g.
`gauge-4c-helm`), and restart: it is a starboard-side unit (starboard
factory pages, which you then change) under its own name, and the Unit
card keeps that name unless you switch its side. Update it with its address:
`--upload-port gauge-4c-helm.local`. With `SECRET_USE_STATIC_IP 1` it would
take the starboard unit's static address: keep that setting at 0 (DHCP), or
add an address for it in the code (`main.cpp`, `unit_identity.h`).

## 3. A new page style

A style is a kind of page (today: Dial, Compass, Sea horizon). A new one
touches these files, in this order. Keep the rules of
[AGENTS.md](../AGENTS.md): host-safe logic with tests, UI code that builds
on the host, and never a stale value shown as live.

| File | What to add |
|---|---|
| `common/src/gauge_defs.h` | A value in `enum class GaugeKind` |
| `common/src/gauge_defs.cpp` | Its template in `style_template()`: default title, path, display unit, range, ticks, listen delay. Paths and ranges live only here. |
| `common/src/page_rules.cpp` | Its name in `page_style_name()` and in the list `parse_page_style()` walks; its name in the **Style** dropdown of `page_schema()`; which fields its card shows (`page_schema()` shows the unit, scale and zones only for a dial) |
| `common/src/gauge_resolve.cpp` | Only if settings and metadata combine differently for it (as for the compass) |
| `common/src/sk_data.cpp` | How its value is read. A number: the existing `StrictFloatListener` path. An object (like `navigation.attitude`): a listener in `sk_listeners.h` and a parser in `sk_parse.h` that accepts only real JSON numbers (`sk_number()`), with tests |
| `common/src/ui/<style>_gauge.{h,cpp}` | The page: a class derived from `GaugePage` (`ui/gauge_page.h`) with `update(value, now_ms)`. Draw the static face once into the canvas; redraw only the moving part, invalidating only the area that changes (see `compass_gauge.cpp`). Dim or park it when the value is stale (`GaugeValue`, `set_stale_look()` in `gauge_page.h`). No Arduino or SensESP here. |
| `common/src/ui/gauge_page.cpp` | A `case` in `create_page()` |
| `tools/preview/preview.cpp` | A sample page in `preview_pages()` |
| `tests/` | `test_page_rules.cpp` (name, parsing, schema), `test_resolve.cpp`, `test_sk_parse.cpp` for a new parser, `test_factory.cpp` if the template matters |
| `docs/PAGES.md` | The style and its settings, for users |

Then:

1. Host tests: `cmake -S tests -B build/tests && cmake --build build/tests -j && build/tests/gauge_tests`.
2. Preview at both sizes and **look at the images**:
   `cmake --build build/preview -j && build/preview/preview 480 && build/preview/preview 720`
   (`out/`). `preview <size> bench` times a redraw in the boards' strip
   heights.
3. Build both boards (`pio run -e p4`, `pio run -e s3`), flash one unit of
   each, and watch the page live, on the 2.1" in particular: its panel
   shares the PSRAM bus with drawing, and memory-heavy drawing shifts the
   picture (AGENTS.md, Gotchas). Check the `loop: Slowest pass` log line.
4. An entry in `docs/JOURNAL.md`.

## 4. A new board

A board is a project folder next to the two existing ones; the gauges
themselves (`common/src`) are shared.

1. **Project**: copy the closer existing one
   (`waveshare-esp32-p4-wifi6-touch-lcd-4c` or
   `waveshare-esp32-s3-touch-lcd-2.1`) and adapt:
   - `platformio.ini`: `board`, flash and PSRAM settings, `lib_deps`
     (keep SensESP and `SignalKGauges=symlink://../common`), and **one env**
     for both sides, named for the board (e.g. `c6`). No per-side env and no
     side `-D` flag: each env compiles everything.
   - `src/main.cpp`: start the display and touch (`start_board()`,
     `lvgl_port_init()` with a draw buffer that fits its RAM), then
     `gauge_app_begin({"<id>", "<label>", SECRET_OTA_HOST_<ENV>_STBD,
     SECRET_OTA_HOST_<ENV>_PORT, button_pin, set_brightness})`. `<id>` goes
     into the hostname (`gauge-<id>-port`), `<label>` into the unit name.
   - `include/lv_conf.h`, and a panel description if ESP32_Display_Panel has
     no built-in entry (the 4"'s `esp_panel_board_custom_conf.h`).
2. **Addresses**: `SECRET_OTA_HOST_<ENV>_STBD` and `_PORT` in
   `include/secrets.example.h` (committed, the mDNS names as defaults) and
   in your `include/secrets.h`.
3. **Dial size**: the UI scales to the panel radius (`Geometry` in `ui/draw_utils.h`); render the
   preview at the new width (`build/preview/preview <width>`) and look.
4. **CI**: the board in the `firmware-build` matrix of
   `.github/workflows/ci.yml` (`project`, `env`).
5. **Docs**: the board table in `AGENTS.md` and `README.md`, a board
   README (hardware, build settings, first power-up checklist), and the
   bring-up in `docs/BRINGUP.md`; then bring it up as that guide says
   before trusting a reading.

## 5. Changing the factory pages

The six pages a fresh unit (or **Reset to factory settings**) starts with
are `kPortPages` / `kStbdPages` in `common/src/gauge_defs.cpp`, by side. Keep
both lists the same length (a `static_assert` checks it), keep paths and
ranges only there, and update `tests/test_factory.cpp` and the table in
[PAGES.md](PAGES.md#factory-pages). Units already set up keep their pages;
only fresh or reset ones get the new defaults. The boat's own path names
(e.g. `stb_engine`) belong in each unit's web UI, not here: the factory
pages stay generic (`port`, `starboard`).
