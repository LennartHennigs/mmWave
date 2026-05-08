# mmWave Monitor

Firmware for Seeed XIAO ESP32-C6 + MR60BHA2 mmWave sensor. Reads breathing rate and heart rate, serves a live WebSocket dashboard, publishes to MQTT with Home Assistant auto-discovery, and sends Pushover push notifications when vital sign thresholds are exceeded.

## Setup

1. Copy `src/config.example.h` → `src/config.h` and fill in your credentials
2. `pio run -t upload` (hold BOOT button, then connect USB)
3. Open `http://mmwave.local` in a browser

## WiFi

List every network to try in `config.h` — the device connects to the strongest available one:

```cpp
#define WIFI_NETWORKS \
  { "home-ssid",   "password" }, \
  { "backup-ssid", "password" },
```

On first boot (or if no configured network is reachable), the device starts a captive portal AP:
- **SSID:** `mmwave` · **Password:** `mmwave1234` · **Portal:** `http://192.168.4.1`

## OTA

Once running on WiFi, flash wirelessly:

```sh
pio run -e seeed_xiao_esp32c6_ota -t upload
```

Requires the device to be reachable at `mmwave.local`.

## Web Dashboard

`http://mmwave.local` — live breathing rate and heart rate, updated via WebSocket every second.

## Alerts

The firmware monitors breathing rate and heart rate against sleep-tuned thresholds and fires edge-triggered events. Two profiles are available (selected at compile time via `VITAL_PROFILE` in `config.h`):

| Profile | Breathing rate | Heart rate |
|---|---|---|
| `PROFILE_ADULT` | 10–20 rpm | 40–100 bpm |
| `PROFILE_TODDLER` (14-month) | 16–45 rpm | 60–160 bpm |

Alert events: `no_breathing`, `low_breathing`, `high_breathing`, `irregular_breathing`, `no_heart_rate`, `low_heart_rate`, `high_heart_rate`, `presence_on`, `presence_off`.

Notification gates in `config.h` let you silence individual categories:

```cpp
#define ALERT_NOTIFY_ONLINE       1   // "mmWave Online" + IP on boot
#define ALERT_NOTIFY_PRESENCE_ON  1   // presence detected
#define ALERT_NOTIFY_PRESENCE_OFF 1   // presence lost
#define ALERT_NOTIFY_BREATHING    1   // breathing alerts
#define ALERT_NOTIFY_HEART_RATE   1   // heart rate alerts
```

## Pushover

Set `ENABLE_PUSHOVER 1` and add your app token and user key to `config.h`. The device sends a notification on boot and on any active alert event. Critical alerts (vital signs) use Pushover priority 1 (bypasses quiet hours); presence and online notifications use priority 0.

## MQTT / Home Assistant

Connects to the broker in `config.h`. Publishes HA MQTT auto-discovery on connect — 10 entities appear automatically:

- `sensor`: Breathing Rate, Heart Rate
- `binary_sensor`: Presence, + 7 alert flags

State topic: `<MQTT_TOPIC_PREFIX>/sensor/state`  
Real-time alert topic: `<MQTT_TOPIC_PREFIX>/alert`

## Debug

`LOG()` output goes to both USB Serial (`DEBUG 1` in `config.h`) and any connected Telnet client:

```sh
telnet mmwave.local
```
