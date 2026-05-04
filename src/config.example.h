#pragma once

// Copy this file to config.h and fill in your values.
// config.h is gitignored and never committed.

// WiFi credentials
#define WIFI_SSID         "your-ssid"
#define WIFI_PASSWORD     "your-password"

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
