# ESP32-C6 co-processor update (4" only)

The 4" board's WiFi is an ESP32-C6 that the ESP32-P4 drives over SDIO
(ESP-Hosted). The P4's Arduino core expects the C6 to run the same
ESP-Hosted version as itself (2.12.11 with Arduino-ESP32 3.3.11). Boards can
leave the factory with an older C6 firmware, and then the boot log says:

```
E (...) ARDUINO: Could not get slave firmware version: ESP_FAIL
I (...) ARDUINO: Slave firmware version: 0.0.0
W (...) ARDUINO: Version on Host is NEWER than version on co-processor
```

WiFi still connects, but the C6 does not answer some of the P4's requests
(the version query above among them), and the caller waits up to 5 s for
each.

This sketch updates the C6 once, over WiFi: it joins the first network from
`include/secrets.h` that answers, downloads the matching image from
`https://espressif.github.io/arduino-esp32/hosted/` (the URL the core's
`ESP_HostedOTA` uses) into PSRAM, and checks it against the size and
SHA-256 pinned in `src/main.cpp` before anything is written to the C6. The
core's own `updateEspHostedSlave()` is not used: it skips the server's
certificate check and writes whatever it is served. An image that does not
match is refused, and the C6 keeps its firmware. The new image is activated
only after it is complete, so an interrupted update leaves the old one in
place; just run it again.

On ESP32-P4 silicon before v3.0 (the chip-id check in `docs/BRINGUP.md`),
use the other env: `pio run -e c6_update_pre_v3 -t upload`.

```bash
cd tools/c6-update
pio run -t upload
pio device monitor
```

Press RESET once the monitor is open. Expect:

```
Co-processor (C6) ESP-Hosted: 0.0.0
Connecting to <ssid>....
Downloading https://.../esp32c6-v2.12.11.bin
...................
SHA-256 5b05e1530e122881ffeeea3ce2e930963cef6fcc79f930cf0994bf9cb9d709cb
Image verified; writing it to the C6
...................
DONE: updated; restarting to check the new version
...
Co-processor (C6) ESP-Hosted: 2.12.11
DONE: the co-processor is up to date, nothing to do
```

Then flash the gauge firmware again (`docs/BRINGUP.md`, step 2). After a
platform update that changes the ESP-Hosted version, the gauge's boot log
shows the warning again. Add the new image to `kImages` in `src/main.cpp`
first. Take its size and SHA-256 from two downloads that agree: the URL
above, and
`https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/hosted/<file>`.
Then run this once more (it refuses a version with no pinned image).
