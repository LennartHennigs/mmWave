# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

```sh
pio run                                    # compile only
pio run -t upload                          # compile + flash via USB (hold BOOT then connect)
pio run -e seeed_xiao_esp32c6_ota -t upload  # OTA upload (device must be online at mmwave.local)
pio device monitor                         # serial monitor at 115200 baud (USB CDC)
```

## Configuration

Copy `src/config.example.h` → `src/config.h` (gitignored) and fill in credentials before flashing. `config.h` is never committed.

## Language

Arduino C/C++ only. No STL, no `std::`, no RTTI. Prefer `char[]` + `snprintf` over `String`. All libraries must be ESP32 Arduino-compatible.

## Architecture

`src/main.cpp` is the single source file:

- **setup():** `Serial` → sensor → WiFi (STA via WiFiMulti or captive-portal AP via WiFiManager) → mDNS → Telnet → OTA → WebSocket → AsyncWebServer → MQTT auto-discovery → alert callbacks
- **loop():** `telnet.loop()` → `ArduinoOTA.handle()` → `mmWave.update(0)` → evaluate alerts every 1 s → deferred Pushover send → WebSocket push every 1 s → MQTT publish every 5 s

**Sensor:** `SEEED_MR60BHA2` on `HardwareSerial(0)` (UART0). Key API: `getBreathRate(float&)`, `getHeartRate(float&)`, `getDistance(float&)`, `isHumanDetected()` — all called inside `if (mmWave.update(0))`.

**Web:** `AsyncWebServer` port 80. Inline HTML served at `/`. `AsyncWebSocket` at `/ws` pushes `{"br":x,"hr":y}` every 1 s. Page JS auto-reconnects on close.

**MQTT:** `PubSubClient` with `setBufferSize(512)`. All topics built from `MQTT_TOPIC_PREFIX` and `MQTT_HA_PREFIX` in `config.h`. Publishes HA auto-discovery on connect; state payload includes breathing rate, heart rate, presence, and all 7 alert flags.

**Telnet:** `ESPTelnet` on port 23. All `LOG()` output goes to both Serial (if `DEBUG 1`) and any connected Telnet client.

**OTA:** `ArduinoOTA` using mDNS hostname `DEVICE_NAME`. PlatformIO env `seeed_xiao_esp32c6_ota` uploads via espota to `mmwave.local`.

## Alert System

`evaluateAlerts()` runs every 1 s. Events are edge-triggered (fire only on state change) and dispatched via `fireAlert()` to all registered `onAlert()` callbacks.

**Debounce:** `ALERT_ZERO_DEBOUNCE_MS` (20 s) for no-signal alerts; `ALERT_DEBOUNCE_MS` (15 s) for low/high threshold alerts. 20 s zero-debounce covers normal toddler central apneas (2.5/hr).

**Profiles:** `VITAL_PROFILE PROFILE_ADULT` or `PROFILE_TODDLER` — selects sleep-tuned thresholds at compile time.

**Pushover:** Deferred — `pushoverHandler()` queues into globals (`_poPending`, `_poTitle`, `_poMsg`, `_poPriority`); actual HTTPS send happens in `loop()` after `mmWave.update()` to avoid blocking sensor reads. Boot sends "mmWave Online" with IP if `ALERT_NOTIFY_ONLINE 1`.

**Notification gates** (all in `config.h`, default 1):
- `ALERT_NOTIFY_ONLINE` — "mmWave Online" + IP on boot
- `ALERT_NOTIFY_PRESENCE_ON` / `ALERT_NOTIFY_PRESENCE_OFF` — presence edge events
- `ALERT_NOTIFY_BREATHING` — no / low / high / irregular breathing
- `ALERT_NOTIFY_HEART_RATE` — no / low / high heart rate

## MQTT Topic Structure

```
<MQTT_HA_PREFIX>/sensor/<MQTT_TOPIC_PREFIX>_breathing_rate/config        ← HA discovery (retained)
<MQTT_HA_PREFIX>/sensor/<MQTT_TOPIC_PREFIX>_heart_rate/config            ← HA discovery (retained)
<MQTT_HA_PREFIX>/binary_sensor/<MQTT_TOPIC_PREFIX>_presence/config       ← HA discovery (retained)
<MQTT_HA_PREFIX>/binary_sensor/<MQTT_TOPIC_PREFIX>_alert_*/config        ← 7 alert entities (retained)
<MQTT_TOPIC_PREFIX>/sensor/state  →  {"breathing_rate":x,"heart_rate":y,"presence":"ON|OFF",
                                       "alert_no_breathing":"ON|OFF", ...7 alert flags}
<MQTT_TOPIC_PREFIX>/alert         →  {"event":"evt_name","value":x}      ← real-time alert events
```

## WiFi

`connectWifi()` tries all networks in `WIFI_NETWORKS` via `WiFiMulti` (strongest available wins). On failure, starts WiFiManager captive portal AP (`mmwave` / `mmwave1234`). Portal times out after 180 s and restarts.

## Partition Scheme

`min_spiffs.csv` — 1.9 MB app partition (vs 1.25 MB default), keeps OTA slot. Required because WiFiMulti adds ~110 KB.
