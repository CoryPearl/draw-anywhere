#pragma once

// Main ESP32-S3 firmware config.
// Keep committed defaults here. Put private Wi-Fi credentials in user_config.local.h.

#if __has_include("user_config.local.h")
#include "user_config.local.h"
#endif

// Wi-Fi used by the ESP32-S3 to reach Firebase.
// Override these in user_config.local.h before flashing.
#ifndef USER_WIFI_SSID
#define USER_WIFI_SSID ""
#endif

#ifndef USER_WIFI_PASSWORD
#define USER_WIFI_PASSWORD ""
#endif

#ifndef USER_BACKEND_URL
#define USER_BACKEND_URL ""
#endif

#ifndef USER_BACKEND_FALLBACK_URL
#define USER_BACKEND_FALLBACK_URL ""
#endif

// Set to 1 for testing if the ESP32 certificate bundle rejects Firebase's cert chain.
#ifndef USER_BACKEND_ALLOW_INSECURE_TLS
#define USER_BACKEND_ALLOW_INSECURE_TLS 1
#endif

// Display behavior.
#ifndef USER_DEFAULT_BRIGHTNESS
#define USER_DEFAULT_BRIGHTNESS 100
#endif

#ifndef USER_BACKEND_POLL_MS
#define USER_BACKEND_POLL_MS 30000
#endif

// Color depth (bitplanes) used while idle vs. while a backend poll's HTTP/TLS
// call is in flight. Fewer bitplanes = shorter refresh cycle + no long dwell
// on the top bitplane, which shrinks the odds a flash-cache stall (mbedTLS/
// HTTP client code isn't IRAM-resident) lands while a row is lit and causes
// a visible flash. Lower value = fewer color shades during that window.
#ifndef USER_COLOR_DEPTH_BITS_IDLE
#define USER_COLOR_DEPTH_BITS_IDLE 3
#endif

#ifndef USER_COLOR_DEPTH_BITS_ACTIVE
#define USER_COLOR_DEPTH_BITS_ACTIVE 1
#endif

#ifndef USER_HUB75_TARGET_CLOCK_HZ
#define USER_HUB75_TARGET_CLOCK_HZ 20000000
#endif

// Extra CPU-cycle delay inside CLK/LAT pulses. Direct GPIO pulses can be too narrow for HUB75 panels.
#ifndef USER_HUB75_PULSE_DELAY_CYCLES
#define USER_HUB75_PULSE_DELAY_CYCLES 8
#endif

// HUB75 RGB matrix wiring.
#ifndef USER_PIN_R1
#define USER_PIN_R1 4
#endif
#ifndef USER_PIN_G1
#define USER_PIN_G1 5
#endif
#ifndef USER_PIN_B1
#define USER_PIN_B1 6
#endif
#ifndef USER_PIN_R2
#define USER_PIN_R2 7
#endif
#ifndef USER_PIN_G2
#define USER_PIN_G2 15
#endif
#ifndef USER_PIN_B2
#define USER_PIN_B2 16
#endif
#ifndef USER_PIN_A
#define USER_PIN_A 8
#endif
#ifndef USER_PIN_B
#define USER_PIN_B 9
#endif
#ifndef USER_PIN_C
#define USER_PIN_C 10
#endif
#ifndef USER_PIN_D
#define USER_PIN_D 11
#endif
#ifndef USER_PIN_E
#define USER_PIN_E 12
#endif
#ifndef USER_PIN_LAT
#define USER_PIN_LAT 13
#endif
#ifndef USER_PIN_OE
#define USER_PIN_OE 14
#endif
#ifndef USER_PIN_CLK
#define USER_PIN_CLK 21
#endif
