# ESP32-S3 LED Matrix Firmware

ESP-IDF firmware for an ESP32-S3 driving a 64x64 HUB75 RGB matrix panel, such as the Waveshare 3 mm pitch 64x64 full-color panel.

The firmware joins your configured Wi-Fi network and polls Firebase Realtime Database
for the newest frame. It no longer creates its own ESP32 access point.

## Build

For step-by-step board setup, wiring, flashing, and testing, see `ESP32_SETUP.md`.

Set Wi-Fi, backend URL, pins, brightness, and poll interval in `main/user_config.h` before flashing.

```sh
cd ESP32Firmware
. /Users/corypearl/.espressif/v6.0.1/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

Flash with:

```sh
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

## Firebase Payload

The web and iOS apps write the latest frame with `PUT` to:

```text
https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json
```

The ESP32 polls the same URL. The stored JSON looks like:

```json
{
  "ok": true,
  "hasFrame": true,
  "width": 64,
  "height": 64,
  "frameHex": "rrggbbrrggbb...",
  "sequence": 123,
  "updatedAt": "2026-07-04T00:00:00Z"
}
```

`frameHex` must contain 4096 RGB pixels as 24-bit hex, row-major order.

The local ESP32 HTTP endpoints are still available for testing:

`POST /brightness`

```json
{ "brightness": 35 }
```

Brightness is `1` to `100`.

`POST /clear` clears the display.

`GET /health` returns display status.

## HUB75 Wiring

Default GPIOs are configured in `main/Kconfig.projbuild`:

| HUB75 | ESP32-S3 GPIO |
| --- | ---: |
| R1 | 4 |
| G1 | 5 |
| B1 | 6 |
| R2 | 7 |
| G2 | 15 |
| B2 | 16 |
| A | 8 |
| B | 9 |
| C | 10 |
| D | 11 |
| E | 12 |
| LAT/STB | 13 |
| OE | 14 |
| CLK | 21 |

Run `idf.py menuconfig` and open `LED Matrix Firmware` to change pins, Wi-Fi SSID, password, backend URL, or default brightness.

For backend relay mode, set:

- `Home Wi-Fi SSID for backend polling`
- `Home Wi-Fi password`
- `Firebase latest-frame URL`

Example ESP32 polling URL:

```text
https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json
```

Use a separate 5 V power supply for the panel. Do not power a 64x64 RGB matrix from the ESP32 board. Tie ESP32 GND and panel power-supply GND together.

## Note

This version uses direct ESP32 GPIO register writes and runs the ESP32-S3 at 240 MHz. `USER_HUB75_TARGET_CLOCK_HZ` is set to 20 MHz as the intended HUB75 clock target, but exact 20 MHz clocking requires a DMA/I2S HUB75 driver. This direct GPIO path is the fastest pure ESP-IDF version in this project.
