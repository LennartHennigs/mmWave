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

`src/main.cpp` is the single source file, split into named helpers:

- **setup():** calls `setupSensor()` → `connectWifi()` → `setupTelnet()` → `setupMdns()` → `setupOTA()` → `setupWebServer()` → `setupMqtt()` → `setupPushover()`
- **loop():** `telnet.loop()` → `ArduinoOTA.handle()` → `kit.update()` → `loopDebugLog()` → `loopPushover()` → `loopWebServer()` → `loopMqtt()`

## mmWaveKit Library (`lib/mmWaveKit/`)

Wraps `SEEED_MR60BHA2` sensor, `BH1750` light sensor, and WS2812 LED. Key API:

- `kit.begin(VitalConfig, LightConfig)` — call once in `setupSensor()`
- `kit.update()` — drain sensor, evaluate alerts every 1 s; call every `loop()`
- `kit.getBreathingRate()` / `kit.getHeartRate()` / `kit.getDistance()` / `kit.getLux()` / `kit.isPresent()`
- `kit.getFirmwareVersion(major, sub, modified)` / `kit.getFirmwareVersion(buf, len)` — returns sensor firmware version; valid after first successful read in `update()`
- `kit.setLedColor(r,g,b)` / `kit.setLedOff()` — library does **not** drive the LED; call from callbacks
- Button2-style callbacks: `kit.onEvent()`, `kit.onPresenceOn/Off()`, `kit.onNoBreathing()`, `kit.onBecameLight/Dark()`, etc.

**VitalConfig:** `profile` (`mmWaveKit::ADULT` or `mmWaveKit::TODDLER`), `zeroDebounceMs` (default 20 s), `threshDebounceMs` (default 15 s).

**LightConfig:** `threshold` (default 10 lux), `trackMode` (default `LIGHT_TRACK_ALWAYS`). Pass `{}` to use all defaults; `LIGHT_TRACK_MODE` from `config.h` flows in via the struct field initializer.

**Library compile isolation:** `mmWaveKit.cpp` never includes `config.h` — `#ifndef` defaults in `mmWaveKit.h` always apply during library compilation (e.g. `trackMode` defaults to `LIGHT_TRACK_ALWAYS` regardless of `config.h`). Config values reach the library only via struct fields set in `main.cpp`.

## Alert System

`kit.update()` evaluates alerts every 1 s. Events are edge-triggered and dispatched via callbacks. Debounce: 20 s for no-signal alerts, 15 s for low/high threshold alerts.

**Profiles:** `VITAL_PROFILE PROFILE_ADULT` or `PROFILE_TODDLER` in `config.h`. `main.cpp` maps this to `mmWaveKit::ADULT` / `mmWaveKit::TODDLER` in `VitalConfig`.

**Pushover:** `pushoverHandler()` queues into globals; send happens in `loopPushover()` to avoid blocking sensor reads. Boot sends "mmWave Online" + IP if `ALERT_NOTIFY_ONLINE 1`. Every notification includes `url=http://DEVICE_NAME.local` ("Open Dashboard"). Notification gates (`ALERT_NOTIFY_BREATHING`, `ALERT_NOTIFY_HEART_RATE`, etc.) in `config.h`.

## Web Dashboard

`AsyncWebServer` port 80. HTML in `src/index_html.h` (PROGMEM). Two cards (Breathing Rate, Heart Rate); footer row 1 shows presence / distance / lux, row 2 shows profile name and tracking mode. WebSocket `/ws` pushes `{"br":x,"hr":y,"presence":bool,"lx":x,"dist":x}` every 1 s. `/info` returns `{"track":n,"threshold":n,"profile":"adult"|"toddler"}`.

## MQTT

`PubSubClient`, `setBufferSize(512)`. HA auto-discovery on connect (2 sensors, 8 binary sensors). State topic: `<prefix>/sensor/state`. Alert topic: `<prefix>/alert`. Reconnect every 30 s, checked each loop (not gated by publish interval).

## WiFi

`WiFiMulti` tries all `WIFI_NETWORKS` (strongest wins). On failure: WiFiManager captive portal AP (`DEVICE_NAME` / `WIFI_AP_PASSWORD`), 180 s timeout then restart.

## Partition Scheme

`min_spiffs.csv` — 1.9 MB app partition (vs 1.25 MB default). Required because WiFiMulti adds ~110 KB.
