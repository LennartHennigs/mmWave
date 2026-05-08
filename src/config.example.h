#pragma once

// Copy this file to config.h and fill in your values.
// config.h is gitignored and never committed.

// WiFi credentials — list every network the device should try.
// WiFiMulti scans and connects to the strongest available one.
// Add or remove rows as needed; at least one entry is required.
#define WIFI_NETWORKS \
  { "primary-ssid",   "primary-password"   }, \
  { "backup-ssid",    "backup-password"    },

// MQTT broker
#define MQTT_HOST         "192.168.x.x"
#define MQTT_PORT         1883
#define MQTT_USER         ""
#define MQTT_PASSWORD     ""

// MQTT topic prefix — all state/discovery topics use these roots.
// e.g. "mmwave" → state on "mmwave/sensor/state"
//                → HA discovery on "homeassistant/sensor/mmwave_breathing_rate/config"
#define MQTT_TOPIC_PREFIX "mmwave"
#define MQTT_HA_PREFIX    "homeassistant"

// Device name: used as mDNS hostname, MQTT client ID, and AP SSID fallback
#define DEVICE_NAME       "mmwave"

// Access Point fallback password (used when STA connection fails)
#define WIFI_AP_PASSWORD  "mmwave1234"

// Set to 1 to enable Serial debug output via USB CDC; 0 compiles it out entirely
#define DEBUG             1

// Feature flags: set to 0 to exclude the feature entirely from the build
#define ENABLE_WEBSERVER  1   // AsyncWebServer + WebSocket dashboard on port 80
#define ENABLE_MQTT       1   // MQTT publishing + Home Assistant auto-discovery
#define ENABLE_PUSHOVER   1   // Pushover push notifications on alert events

// OTA
#define ENABLE_OTA        1   // ArduinoOTA wireless flashing
#define OTA_PASSWORD      ""

// ---------------------------------------------------------------------------
// Pushover push notifications
// Get your app token at https://pushover.net/apps/build
// Get your user key at https://pushover.net (top of dashboard)
// ---------------------------------------------------------------------------
#define PUSHOVER_APP_TOKEN  "your_app_token_here"
#define PUSHOVER_USER_KEY   "your_user_key_here"

// ---------------------------------------------------------------------------
// Alert notification gates — set to 0 to silence a category on all channels
// (Pushover, MQTT alert topic, and any future onAlert() callbacks).
// ---------------------------------------------------------------------------
#define ALERT_NOTIFY_ONLINE       1   // notify with IP address when device comes online
#define ALERT_NOTIFY_PRESENCE_ON  1   // notify when presence is detected
#define ALERT_NOTIFY_PRESENCE_OFF 1   // notify when presence is lost
#define ALERT_NOTIFY_BREATHING    1   // no / low / high / irregular breathing
#define ALERT_NOTIFY_HEART_RATE   1   // no / low / high heart rate

// ---------------------------------------------------------------------------
// Vital sign alert thresholds — bedtime / sleep monitor
//
// VITAL_PROFILE selects the threshold set at compile time:
//   PROFILE_ADULT   — sleeping adult  (HR 40-100 bpm, RR 10-20 rpm)
//   PROFILE_TODDLER — 14-month toddler sleeping (HR 60-160 bpm, RR 16-45 rpm)
//
// Sources: Fleming 2011 (Lancet), Natarajan 2021 (NPJ Dig. Med.),
//          Vézina 2020 (Ann. Am. Thoracic Soc.), Nanchen 2018 (Heart)
// ---------------------------------------------------------------------------
#define PROFILE_ADULT   0
#define PROFILE_TODDLER 1

#define VITAL_PROFILE   PROFILE_ADULT   // ← change to PROFILE_TODDLER for infant monitoring
