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

This sketch updates the C6 once, over WiFi, with the core's own
`ESP_HostedOTA` library: it joins the first network from
`include/secrets.h` that answers, downloads the matching image from
`https://espressif.github.io/arduino-esp32/hosted/` and writes it to the
C6. The new image is activated only after it is complete, so an interrupted
download leaves the old one in place; just run it again.

```bash
cd tools/c6-update
pio run -t upload
pio device monitor
```

Press RESET once the monitor is open. Expect:

```
Co-processor (C6) ESP-Hosted: 0.0.0
Connecting to <ssid>....
Updating esp-hosted co-processor from https://.../esp32c6-v2.12.11.bin
........................................
SUCCESS: esp-hosted co-processor updated!
DONE: updated; restarting to check the new version
...
Co-processor (C6) ESP-Hosted: 2.12.11
DONE: the co-processor is up to date, nothing to do
```

Then flash the gauge firmware again (`docs/BRINGUP.md`, step 2). After a
platform update that changes the ESP-Hosted version, the gauge's boot log
shows the warning again; run this once more.
