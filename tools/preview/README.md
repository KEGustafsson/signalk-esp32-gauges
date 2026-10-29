# Gauge preview on a PC

Renders every gauge page with the firmware's own drawing code
(`common/src/ui`) and LVGL 9.3, at the 2.1" (480) or 4" (720) resolution,
with fixed sample values. Use it to try out ranges, zones or colours in
`common/src/gauge_defs.cpp` without flashing a board.

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
