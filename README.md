# Draw Anywhere

Draw Anywhere is a 64x64 RGB LED matrix drawing project. It includes:

- A static HTML drawing app in `StaticHTML/`
- A SwiftUI iOS app in `LEDMatrixDrawApple/`
- ESP-IDF firmware for an ESP32-S3 in `ESP32Firmware/`
- A Firebase Realtime Database relay for sending drawings to the ESP32
- A legacy PHP backend in `PHPBackend/` kept as an optional fallback

The current active backend is Firebase. Keep your actual Firebase URL in local ignored config files, not in committed source.

## How It Works

1. Draw a 64x64 image in the web app or iOS app.
2. Press `Send`.
3. The frontend writes the latest frame to Firebase as JSON.
4. The ESP32-S3 connects to Wi-Fi and polls Firebase.
5. When a new frame appears, the ESP32 updates the HUB75 RGB matrix.

The frame is stored as compact RGB hex:

```json
{
  "ok": true,
  "hasFrame": true,
  "width": 64,
  "height": 64,
  "frameHex": "rrggbbrrggbb...",
  "sequence": 123,
  "updatedAt": "2026-08-18T00:00:00Z"
}
```

`frameHex` contains 4096 pixels, row-major, with each pixel as 6 hex characters.

## Web App

Open:

```text
StaticHTML/index.html
```

If browser security blocks local requests, serve it locally:

```sh
cd StaticHTML
python3 -m http.server 5500
```

Then open:

```text
http://127.0.0.1:5500
```

Features include brush, eraser, fill, line, rectangle, ellipse, text, image import, drag/drop images, undo/redo, dark mode, and Firebase URL settings.

## iOS App

Open this project in Xcode:

```text
LEDMatrixDrawApple/LEDMatrixDraw.xcodeproj
```

Build and run the `LEDMatrixDraw` scheme on a simulator or real iPhone. Add your backend URL in Settings before sending drawings.

## Firebase Setup

For initial testing, set Firebase Realtime Database rules to allow reads/writes to the matrix path:

```json
{
  "rules": {
    "matrix": {
      ".read": true,
      ".write": true
    }
  }
}
```

See `FIREBASE_SETUP.md` for more detail.

## ESP32-S3 Firmware

Firmware lives in:

```text
ESP32Firmware/
```

Edit:

```text
ESP32Firmware/main/user_config.h
```

Set your Wi-Fi:

```c
#define USER_WIFI_SSID "Your Wi-Fi Name"
#define USER_WIFI_PASSWORD "Your Wi-Fi Password"
```

Put your private backend URL in the ignored local config:

```c
#define USER_BACKEND_URL "https://your-database.firebaseio.com/matrix/latest.json"
```

Build and flash:

```sh
cd ESP32Firmware
source /Users/corypearl/.espressif/v6.0.1/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Replace `/dev/cu.usbmodemXXXX` with your actual ESP32 serial port.

## HUB75 Wiring

Default GPIOs:

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
| GND | GND |

Power the LED matrix from a separate 5 V supply. Do not power the matrix from the ESP32 board. Tie ESP32 GND and matrix power-supply GND together.

## Expected ESP32 Logs

On boot, the panel starts green. After Wi-Fi connects, it turns blue. If the first backend poll fails, it turns red.

Successful Firebase polling should show:

```text
Wi-Fi got IP: ...
Backend polling enabled: ...
Applied backend matrix frame: lit_pixels=...
```

If `lit_pixels=0`, Firebase currently contains a blank drawing.

## Notes

- The ESP32 firmware currently uses direct GPIO register refresh, not a full DMA HUB75 driver.
- `USER_HUB75_TARGET_CLOCK_HZ` is set to 20 MHz as the intended target.
- `USER_HUB75_PULSE_DELAY_CYCLES` can be adjusted if the panel does not latch data reliably.
- TLS certificate verification is currently relaxed for ESP32 Firebase testing. For production, use a proper CA/root certificate setup and disable insecure TLS.
- `PHPBackend/` is legacy and not required for the current Firebase setup.
