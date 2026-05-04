# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

```sh
pio run                 # compile only
pio run -t upload       # compile + flash (auto-detects XIAO via USB VID:PID 0x303A:0x1001)
pio device monitor      # serial monitor at 115200 baud (USB CDC)
```

## Configuration

Copy `src/config.example.h` → `src/config.h` (gitignored) and fill in credentials before flashing. `config.h` is never committed.

## Language

Arduino C/C++ only. No STL, no `std::`, no RTTI. Prefer `char[]` + `snprintf` over `String`. All libraries must be ESP32 Arduino-compatible.

## Architecture

`src/main.cpp` is the single source file:

- **setup():** `Serial` → sensor → WiFi (STA or captive-portal AP via WiFiManager) → mDNS → WebSocket → AsyncWebServer → MQTT auto-discovery
- **loop():** `mmWave.update(100)` → WebSocket push every 1 s → MQTT publish every 5 s → `mqttClient.loop()`

**Sensor:** `SEEED_MR60BHA2` on `HardwareSerial(0)` (UART0). Key API: `getBreathRate(float&)`, `getHeartRate(float&)`, `getDistance(float&)` — all called inside `if (mmWave.update(100))`.

**Web:** `AsyncWebServer` port 80. Inline HTML served at `/`. `AsyncWebSocket` at `/ws` pushes `{"br":x,"hr":y}` every 1 s. Page JS auto-reconnects on close.

**MQTT:** `PubSubClient` with `setBufferSize(512)` (HA discovery payloads exceed the 256-byte default). All topics are built from `MQTT_TOPIC_PREFIX` and `MQTT_HA_PREFIX` in `config.h`.

## MQTT Topic Structure

```
<MQTT_HA_PREFIX>/sensor/<MQTT_TOPIC_PREFIX>_breathing_rate/config   ← HA discovery (retained)
<MQTT_HA_PREFIX>/sensor/<MQTT_TOPIC_PREFIX>_heart_rate/config       ← HA discovery (retained)
<MQTT_HA_PREFIX>/binary_sensor/<MQTT_TOPIC_PREFIX>_presence/config  ← HA discovery (retained)
<MQTT_TOPIC_PREFIX>/sensor/state  →  {"breathing_rate":x,"heart_rate":y,"presence":true|false}
```

## WiFi Fallback

`connectWifi()` tries `WIFI_SSID`/`WIFI_PASSWORD` for 10 s. On failure, starts WiFiManager captive portal AP (`mmwave` / `mmwave1234`). Portal times out after 180 s and restarts the device.
