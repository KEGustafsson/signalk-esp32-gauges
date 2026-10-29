# Waveshare ESP32-P4-WIFI6-Touch-LCD-4C (SKU 31522)

Signal K steel gauges for the 4" round board. See the
[top-level README](../README.md) for the gauges, touch controls and setup.

| Unit | Side | Address | OTA | First flash |
|---|---|---|---|---|
| 4" #1 | starboard | 192.168.220.130 | `pio run -e p4_stbd -t upload` | `pio run -e p4_stbd_usb -t upload` |
| 4" #2 | port | 192.168.220.131 | `pio run -e p4_port -t upload` | `pio run -e p4_port_usb -t upload` |

## Hardware

From the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4C),
Waveshare's ESP-IDF BSP (`waveshare/esp32_p4_wifi6_touch_lcd_xc`) and their
Arduino examples ([ESP32-P4-WIFI6-Touch-LCD-XC](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-XC)):

| Part | Details | Used here |
|---|---|---|
| MCU | ESP32-P4 (dual-core RISC-V up to 360/400 MHz), 32 MB PSRAM, 32 MB flash | yes |
| Wireless | ESP32-C6-MINI-1 over SDIO (esp-hosted): WiFi 6 2.4 GHz, BLE 5 | WiFi, through Arduino's `WiFi.h` |
| Display | 4" round IPS 720×720, JD9365, 2-lane MIPI-DSI at 1500 Mbps/lane, 80 MHz DPI clock, reset GPIO27, DSI PHY on LDO channel 3 | yes |
| Touch | GT9271 (GT911-compatible) on I2C SDA 7 / SCL 8, address 0x5D (0x14 alternative), reset/INT not routed | swipe / tap / hold |
| Backlight | GPIO26, LEDC PWM, active low | brightness (long press) |
| Audio | ES8311 codec + NS4150B amplifier (speaker), ES7210 mic ADC; I2S MCLK 13, SCLK 12, LRCK 10, DOUT 9, DIN 11, PA enable GPIO53 | not used |
| microSD | 4-bit SDIO: D0–D3 GPIO39–42, CLK 43, CMD 44 | not used |
| Camera | MIPI-CSI 2-lane connector | not used |
| USB | USB-C (USB Serial/JTAG + UART), USB-A 2.0 OTG high speed | flashing, logs |
| Other | 40-pin header, RTC battery holder, BOOT and RESET buttons | not used |

ESP32_Display_Panel has no built-in entry for this board, so
[`include/esp_panel_board_custom_conf.h`](include/esp_panel_board_custom_conf.h)
describes it. Its JD9365 init table is copied verbatim from Waveshare's BSP
(720×720 variant); it is byte-identical to the 4C table in Waveshare's Arduino
examples.

## Build settings

- Board `esp32-p4_r3` (ESP32-P4 silicon v3.x, Waveshare's default). If your
  board has pre-v3 silicon (the boot log prints the chip revision), change it to
  `board = esp32-p4`.
- 32 MB flash with `partitions_32MB.csv`: two 8 MB OTA slots and a ~16 MB
  filesystem for SensESP.
- Serial is the USB Serial/JTAG on the USB-C port (`ARDUINO_USB_MODE=1`,
  `ARDUINO_USB_CDC_ON_BOOT=1`), as in Waveshare's Arduino setup.
- LVGL renders into two 720×72-pixel buffers and copies them into the DPI
  panel's frame buffer.

## First power-up checklist

- The gauge appears with dashes in the LCD before WiFi connects.
- If touch does not respond, the GT911 is probably at its alternative address:
  set `ESP_PANEL_BOARD_TOUCH_I2C_ADDRESS` to `(0x14)` in
  `include/esp_panel_board_custom_conf.h`.
- If the backlight stays dark (or full brightness ignores the long press),
  flip `ESP_PANEL_BOARD_BACKLIGHT_ON_LEVEL` in the same file; Waveshare's BSP
  drives this pin inverted.
- If WiFi never connects, check the boot log for esp-hosted errors: the
  ESP32-C6 firmware must be compatible with the esp-hosted version in
  Arduino-ESP32 3.3.11. Waveshare's documentation covers updating the C6.
- Tap shows the IP address; it should match the table above.
