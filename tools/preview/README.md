# Gauge preview on a PC

Renders the factory pages (`common/src/gauge_defs.cpp`) and two example
user-defined dials with the firmware's own drawing code (`common/src/ui`,
through `resolve_gauge()`) and LVGL 9.3, at the 2.1" (480) or 4" (720)
resolution, with fixed sample values and sample Signal K zones. Use it to
try out styles, ranges, zones or colours without flashing a board.

```bash
cmake -S tools/preview -B build/preview -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build/preview -j
```

```bash
mkdir -p out && build/preview/preview 480 && build/preview/preview 720
```

```bash
python3 tools/preview/contact_sheet.py out/480 out/preview-480.png
```

The images land in `out/` as PPM files; `contact_sheet.py` (needs Pillow)
combines them into one PNG. Each page is rendered twice: live
(`out/<size>_*.ppm`) and after its sample has gone stale
(`out/stale<size>_*.ppm`, e.g. `contact_sheet.py out/stale480 ...`), which
must show dashes, a parked needle, a dimmed compass card or a dimmed, level
boat symbol.

It also renders the Crew Radio mock-ups (`out/radio<size>_*.ppm`; sheet with
`contact_sheet.py out/radio720 ...`): the radio halo over gauge pages
(someone talking, two talkers, over Reticulum, muted through a relay, this
unit transmitting, no clock) and the Crew page, on a gauge and as the
standalone station's talk-disc screen. They are drawn by
`common/src/ui/radio_halo.*` and `crew_page.*` from sample `CrewView`s
(`crew_view.h`); no radio code exists yet, so nothing in the firmware shows
them.

`build/preview/preview 480 bench` also times one redraw of each page while
its value swings (the median of the 240 frames that redrew anything; a
frame at an unchanged value is left out), drawn in strips as tall as the board's
draw buffer. PC times are only relative: a compass step that takes ~2 ms
here took ~150 ms on the 2.1". And they say nothing about memory traffic,
which is what the 2.1"'s panel cannot spare (`AGENTS.md`, Gotchas): check a
drawing change on the real screen too.
