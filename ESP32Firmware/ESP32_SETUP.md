# ESP32-S3 Setup Guide

This guide is for flashing the ESP32-S3 firmware and connecting it to the 64x64 HUB75 RGB matrix and Firebase backend.

## What You Need

- ESP32-S3 board
- Waveshare 64x64 RGB LED matrix panel, HUB75 style
- Separate 5 V power supply for the LED panel
- USB cable for the ESP32-S3
- Common ground between ESP32 and LED panel power supply

Do not power the LED matrix from the ESP32 board.

## Firebase URL

The firmware, web app, and iOS app are already set to use:

```text
https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json
```

## Wiring

Default GPIOs are configured in `main/Kconfig.projbuild`.

| HUB75 Pin | ESP32-S3 GPIO |
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
| LAT / STB | 13 |
| OE | 14 |
| CLK | 21 |
| GND | GND |

Power the matrix from the external 5 V supply. Connect the power supply GND to ESP32 GND.

## Configure Wi-Fi and Backend URL

The easiest setup is editing:

```text
ESP32Firmware/main/user_config.h
```

Set Wi-Fi and backend:

```c
#define USER_WIFI_SSID "Your Wi-Fi Name"
#define USER_WIFI_PASSWORD "Your Wi-Fi Password"
#define USER_BACKEND_URL "https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json"
#define USER_BACKEND_ALLOW_INSECURE_TLS 0
```

If `USER_WIFI_SSID` is empty, the ESP32 will not be able to reach the backend. Set your Wi-Fi name and password before flashing.

The same file also contains:

- Default brightness
- Backend poll interval
- HUB75 RGB matrix GPIO pins
- TLS verification toggle for the backend URL
- HUB75 target clock setting

Only use `menuconfig` if you want to change advanced ESP-IDF settings.

The current config uses Firebase HTTPS with normal TLS verification. If Firebase returns `Permission denied`, update your Firebase Realtime Database rules to allow this test device to read and write.
See `../FIREBASE_SETUP.md` for the test rules.

## Optional Menuconfig

From this folder:

```sh
cd /Users/corypearl/Desktop/draw-app/ESP32Firmware
. /Users/corypearl/.espressif/v6.0.1/esp-idf/export.sh
idf.py menuconfig
```

Open:

```text
LED Matrix Firmware
```

You can set:

- `Home Wi-Fi SSID for backend polling`
- `Home Wi-Fi password`
- `Firebase latest-frame URL`

Values in `main/user_config.h` are the main project config. Prefer changing that file for normal setup.

Save and exit.

## Find the ESP32 Port

Plug in the ESP32-S3, then run:

```sh
ls /dev/cu.*
```

Common port names look like:

```text
/dev/cu.usbmodemXXXX
/dev/cu.usbserial-XXXX
```

Use the new USB port that appears after plugging in the ESP32.

## Build and Flash

```sh
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Replace `/dev/cu.usbmodemXXXX` with your actual port.

If flashing fails, hold the board `BOOT` button while the flash starts, then release it after writing begins.

## Expected Serial Output

In `monitor`, look for messages like:

```text
Connecting to Wi-Fi SSID ...
Wi-Fi got IP: 192.168.x.x
Backend polling enabled: https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json
Applied backend matrix frame
```

The ESP32 no longer starts a local setup access point. It uses the Wi-Fi network configured in `main/user_config.h`.
If you do not see `Wi-Fi got IP`, the ESP32 is not fully connected to Wi-Fi yet. If you do see `Wi-Fi got IP` but then see `getaddrinfo()` errors, the Wi-Fi network's DNS cannot resolve the Firebase host.

## Test Flow

1. Open the drawing frontend.
2. Draw something.
3. Press `Send`.
4. Open:

```text
https://draw-anywhere-8ff7d-default-rtdb.firebaseio.com/matrix/latest.json
```

You should see a large JSON response with `width`, `height`, `frameHex`, and `sequence`.

5. With the ESP32 flashed and connected to Wi-Fi, it should poll that JSON and update the panel.

## Notes

This firmware uses direct GPIO register refresh, runs the ESP32-S3 at 240 MHz, and sets `USER_HUB75_TARGET_CLOCK_HZ` to 20 MHz as the intended HUB75 clock target. For an exact 20 MHz panel clock and the least flicker, the next step is a DMA/I2S HUB75 driver.
