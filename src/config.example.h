#pragma once

// Copy this file to config.h and fill in your values.
// config.h is gitignored and never committed.

// WiFi credentials — list every network the device should try.
// WiFiMulti scans and connects to the strongest available one.
// Add or remove rows as needed; at least one entry is required.
#define WIFI_NETWORKS \
  { "primary-ssid",   "primary-password"   }, \
  { "backup-ssid",    "backup-password"    },

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
// recommend to keep OTA and telnet enabled for remote maintenance and debugging
#define ENABLE_OTA        1   // ArduinoOTA wireless flashing
#define ENABLE_TELNET     1   // ESPTelnet debug log on port 23

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
#if ENABLE_MQTT
  #define MQTT_HOST         "192.168.x.x"
  #define MQTT_PORT         1883
  #define MQTT_USER         ""
  #define MQTT_PASSWORD     ""

  // Topic prefix — e.g. "mmwave" → state on "mmwave/sensor/state"
  //                              → HA discovery on "homeassistant/sensor/mmwave_breathing_rate/config"
  #define MQTT_TOPIC_PREFIX "mmwave"
  #define MQTT_HA_PREFIX    "homeassistant"
#endif // ENABLE_MQTT

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------
#if ENABLE_OTA
  #define OTA_PASSWORD      ""
#endif // ENABLE_OTA

// ---------------------------------------------------------------------------
// Pushover push notifications
// Get your app token at https://pushover.net/apps/build
// Get your user key at https://pushover.net (top of dashboard)
// ---------------------------------------------------------------------------
#if ENABLE_PUSHOVER
  #define PUSHOVER_APP_TOKEN  "your_app_token_here"
  #define PUSHOVER_USER_KEY   "your_user_key_here"
#endif // ENABLE_PUSHOVER

// ---------------------------------------------------------------------------
// Alert notification gates — set to 0 to silence a category on all channels
// ---------------------------------------------------------------------------
#define ALERT_NOTIFY_ONLINE       1   // notify with IP address when device comes online
#define ALERT_NOTIFY_PRESENCE_ON  1   // notify when presence is detected
#define ALERT_NOTIFY_PRESENCE_OFF 1   // notify when presence is lost
#define ALERT_NOTIFY_BREATHING    1   // no / low / high / irregular breathing
#define ALERT_NOTIFY_HEART_RATE   1   // no / low / high heart rate

// ---------------------------------------------------------------------------
// Vital sign profile — selects preset thresholds from mmWaveKit library:
//   PROFILE_ADULT   — sleeping adult  (BR 10-20 rpm, HR 40-100 bpm)
//   PROFILE_TODDLER — toddler sleeping (BR 16-45 rpm, HR 60-160 bpm)
// ---------------------------------------------------------------------------
#define VITAL_PROFILE   PROFILE_ADULT   // PROFILE_ADULT or PROFILE_TODDLER

// ---------------------------------------------------------------------------
// Light sensor (BH1750 I2C on D4/D5) + data tracking mode
// LIGHT_TRACK_ALWAYS/DARK/LIGHT constants are provided by mmWaveKit.h
// ---------------------------------------------------------------------------
#define LIGHT_TRACK_MODE     LIGHT_TRACK_ALWAYS
