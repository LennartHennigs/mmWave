# mmWave Monitor

Firmware for Seeed XIAO ESP32-C6 + MR60BHA2 mmWave sensor. Reads breathing rate and heart rate, publishes to MQTT (Home Assistant auto-discovery), and serves a live WebSocket dashboard.

## Setup

1. Copy `src/config.example.h` → `src/config.h` and fill in your WiFi and MQTT credentials
2. `pio run -t upload`
3. Open `http://mmwave.local` or `http://<ip>` in a browser

## WiFi

On first boot (or if the configured SSID is unreachable), the device starts an AP:
- **SSID:** `mmwave`
- **Password:** `mmwave1234`
- **Portal:** `http://192.168.4.1`

## Web UI

- `http://mmwave.local` — live breathing rate and heart rate, updated via WebSocket every second

## MQTT

Connects to the broker in `config.h`. Publishes Home Assistant MQTT auto-discovery on connect. State topic: `<MQTT_TOPIC_PREFIX>/sensor/state`.

## Debug

Set `#define DEBUG 1` in `config.h` to enable serial output via USB CDC at 115200 baud (`pio device monitor`).
