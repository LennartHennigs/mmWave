# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- Switch platform to `pioarduino/platform-espressif32` (community fork). The official `espressif32` platform ships Arduino-ESP32 2.0.17 which has no ESP32-C6 Arduino framework support; C6 requires Arduino-ESP32 3.x, bundled in pioarduino.
- Simplified `.gitignore` to exclude the entire `.vscode/` directory instead of individual files.

## [0.1.0] - 2026-05-04

### Added

- PlatformIO project for Seeed XIAO ESP32-C6 + MR60BHA2 mmWave sensor
- Sensor polling via `SEEED_MR60BHA2` on `HardwareSerial(0)` (UART0); reads breathing rate, heart rate, and presence
- WiFi connection from `config.h` credentials with 10 s timeout; falls back to WiFiManager captive portal AP (`mmwave` / `WIFI_AP_PASSWORD`) on failure
- mDNS hostname `mmwave.local`
- `AsyncWebServer` on port 80 serving an inline HTML dashboard
- `AsyncWebSocket` at `/ws` pushing `{"br":x,"hr":y}` every 1 s to connected clients
- MQTT publishing via `PubSubClient` with Home Assistant auto-discovery on connect (breathing rate, heart rate, binary presence sensors)
- All MQTT topics configurable via `MQTT_TOPIC_PREFIX` and `MQTT_HA_PREFIX` in `config.h`
- `DEBUG` flag in `config.h` gates all `Serial` output via `LOG()` macro; compiles out completely when `0`
- USB CDC enabled via `ARDUINO_USB_CDC_ON_BOOT=1` — no UART adapter needed
- Auto-detection of XIAO ESP32-C6 USB port via VID:PID `0x303A:0x1001`
- `src/config.example.h` template; `src/config.h` is gitignored

### Fixed

- Replaced `getDistance()` return value with `isHumanDetected()` for accurate presence detection
- Changed binary sensor MQTT state to `"ON"`/`"OFF"` strings (HA-idiomatic); removed `payload_on`/`payload_off` type mismatch
- Added 30 s MQTT reconnect backoff; publish fires immediately after reconnect rather than waiting another 5 s
- Replaced `mmWave.update(100)` (100 ms blocking spin) with `update(0)` for non-blocking loop
- `ws.textAll()` guarded by `ws.count() > 0` to skip work when no clients are connected
- Extracted `publishEntity()` helper to eliminate three near-identical HA discovery `snprintf`/`publish` blocks
- Removed unused `sensorDist` global; demoted to loop-local variable
- Moved hardcoded AP password to `WIFI_AP_PASSWORD` constant in `config.example.h`
- Widened WebSocket JSON buffer from 48 to 64 bytes
