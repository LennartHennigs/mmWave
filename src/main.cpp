#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiManager.h>
#include <HardwareSerial.h>
#include "Seeed_Arduino_mmWave.h"
#include "config.h"
#include <ESPTelnet.h>

#if ENABLE_WEBSERVER || ENABLE_OTA
  #include <ESPmDNS.h>
#endif

#if ENABLE_WEBSERVER
  #include <AsyncTCP.h>
  #include <ESPAsyncWebServer.h>
#endif

#if ENABLE_OTA
  #include <ArduinoOTA.h>
#endif

#if ENABLE_MQTT
  #include <PubSubClient.h>
#endif

#if ENABLE_PUSHOVER
  #include "Pushover.h"
#endif

// ---------------------------------------------------------------------------
// Telnet + logging — always active; Serial output gated on DEBUG
// ---------------------------------------------------------------------------
ESPTelnet telnet;

static void logPrint(const char* fmt, ...) {
#if !DEBUG
  if (!telnet.isConnected()) return;
#endif
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
#if DEBUG
  Serial.print(buf);
#endif
  telnet.print(buf);
}
#define LOG(fmt, ...) logPrint("[DBG] " fmt "\n", ##__VA_ARGS__)

#ifndef WIFI_AP_PASSWORD
  #define WIFI_AP_PASSWORD "mmwave1234"
#endif

// UART0 for sensor; Serial maps to USB CDC via ARDUINO_USB_CDC_ON_BOOT — no conflict
HardwareSerial  mmWaveSerial(0);
SEEED_MR60BHA2  mmWave;

#if ENABLE_WEBSERVER
AsyncWebServer  server(80);
AsyncWebSocket  ws("/ws");
static unsigned long lastWsPush = 0;
#endif

#if ENABLE_MQTT
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
static unsigned long lastMqttPub     = 0;
static unsigned long lastMqttAttempt = 0;
#endif

static float breathingRate = 0.0f;
static float heartRate     = 0.0f;
static bool  presence      = false;

// WiFi network list — populated from WIFI_NETWORKS in config.h.
// Backwards-compat: if config.h still uses the old WIFI_SSID/WIFI_PASSWORD
// pair, wrap it automatically so no manual migration is required.
#ifndef WIFI_NETWORKS
  #ifdef WIFI_SSID
    #define WIFI_NETWORKS { WIFI_SSID, WIFI_PASSWORD },
  #else
    #error "config.h must define either WIFI_NETWORKS or WIFI_SSID/WIFI_PASSWORD"
  #endif
#endif

struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred wifiNetworks[] = { WIFI_NETWORKS };
static const uint8_t  wifiNetworkCount = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);

// ---------------------------------------------------------------------------
// Alert system — thresholds, events, callbacks
// ---------------------------------------------------------------------------

// Vital sign profiles: tuned for bedtime / sleep monitoring.
// Sources: Fleming 2011 (Lancet, 1114 citations), Natarajan 2021 (NPJ Dig. Med.),
//          Vézina 2020 (Ann. Am. Thoracic Soc.), Nanchen 2018 (Heart)
#define PROFILE_ADULT   0
#define PROFILE_TODDLER 1

#ifndef VITAL_PROFILE
  #define VITAL_PROFILE PROFILE_ADULT
#endif

#if VITAL_PROFILE == PROFILE_TODDLER
  // 14-month toddler sleeping: median HR ~100-110 bpm, RR ~22-30 rpm.
  // Central apneas of 2.5/hr (up to ~20 s) are normal at this age.
  #define ALERT_BR_LOW           16.0f  // rpm — below sleep RR floor (~22)
  #define ALERT_BR_HIGH          45.0f  // rpm — tachypnea during sleep
  #define ALERT_HR_LOW           60.0f  // bpm — well below normal sleep HR (~100-110)
  #define ALERT_HR_HIGH         160.0f  // bpm — sustained tachycardia
  #define ALERT_BR_IRREG_STDDEV   5.0f  // rpm std-dev (wider normal variance in toddlers)
#else
  // Adult sleeping: nocturnal HR 40-80 bpm, RR 90th-pct range 11.8-19.2 rpm.
  #define ALERT_BR_LOW           10.0f  // rpm — below nocturnal 90th-pct floor (11.8)
  #define ALERT_BR_HIGH          20.0f  // rpm — above nocturnal 90th-pct ceiling (19.2)
  #define ALERT_HR_LOW           40.0f  // bpm — deep sleep can reach 40-50; below 40 abnormal
  #define ALERT_HR_HIGH         100.0f  // bpm — tachycardia during sleep
  #define ALERT_BR_IRREG_STDDEV   3.0f  // rpm std-dev over 60 s window
#endif

// Shared debounce timings
#define ALERT_ZERO_DEBOUNCE_MS  20000UL  // "no signal" alert (covers normal toddler apneas)
#define ALERT_DEBOUNCE_MS       15000UL  // sustained threshold before low/high alert fires
#define ALERT_IRREG_WINDOW         60    // samples at 1/s = 60 s rolling window

// Per-category notification gates — set to 0 to silence a category across all channels.
// Channels: Pushover, MQTT alert topic, any future onAlert() callback.
#ifndef ALERT_NOTIFY_ONLINE
  #define ALERT_NOTIFY_ONLINE       1
#endif
#ifndef ALERT_NOTIFY_PRESENCE_ON
  #define ALERT_NOTIFY_PRESENCE_ON  1
#endif
#ifndef ALERT_NOTIFY_PRESENCE_OFF
  #define ALERT_NOTIFY_PRESENCE_OFF 1
#endif
#ifndef ALERT_NOTIFY_BREATHING
  #define ALERT_NOTIFY_BREATHING    1
#endif
#ifndef ALERT_NOTIFY_HEART_RATE
  #define ALERT_NOTIFY_HEART_RATE   1
#endif

// Events fired to registered callbacks (edge-triggered — only on state change)
enum AlertEvent {
  EVT_PRESENCE_ON,
  EVT_PRESENCE_OFF,
  EVT_NO_BREATHING,
  EVT_LOW_BREATHING,
  EVT_HIGH_BREATHING,
  EVT_IRREGULAR_BREATHING,
  EVT_NO_HEART_RATE,
  EVT_LOW_HEART_RATE,
  EVT_HIGH_HEART_RATE,
};

struct AlertState {
  bool noBreathing;
  bool lowBreathing;
  bool highBreathing;
  bool irregularBreathing;
  bool noHeartRate;
  bool lowHeartRate;
  bool highHeartRate;
};
static AlertState alerts     = {};
static AlertState prevAlerts = {};

// Callback registry — register handlers with onAlert()
typedef void (*AlertCallback)(AlertEvent event, float value);

#define MAX_ALERT_CALLBACKS 4
static AlertCallback alertCallbacks[MAX_ALERT_CALLBACKS];
static uint8_t alertCallbackCount = 0;

void onAlert(AlertCallback cb) {
  if (alertCallbackCount < MAX_ALERT_CALLBACKS)
    alertCallbacks[alertCallbackCount++] = cb;
}

static void fireAlert(AlertEvent event, float value) {
  switch (event) {
    case EVT_PRESENCE_ON:
      if (!ALERT_NOTIFY_PRESENCE_ON)  return; break;
    case EVT_PRESENCE_OFF:
      if (!ALERT_NOTIFY_PRESENCE_OFF) return; break;
    case EVT_NO_BREATHING:
    case EVT_LOW_BREATHING:
    case EVT_HIGH_BREATHING:
    case EVT_IRREGULAR_BREATHING:
      if (!ALERT_NOTIFY_BREATHING)  return; break;
    case EVT_NO_HEART_RATE:
    case EVT_LOW_HEART_RATE:
    case EVT_HIGH_HEART_RATE:
      if (!ALERT_NOTIFY_HEART_RATE) return; break;
  }
  for (uint8_t i = 0; i < alertCallbackCount; i++)
    alertCallbacks[i](event, value);
}

// Rolling window for irregular breathing detection (two-pass std-dev)
static float   brWindow[ALERT_IRREG_WINDOW];
static uint8_t brWindowIdx  = 0;
static uint8_t brWindowFill = 0;

static float brStddev() {
  if (brWindowFill < 2) return 0.0f;
  float mean = 0.0f;
  for (uint8_t i = 0; i < brWindowFill; i++) mean += brWindow[i];
  mean /= brWindowFill;
  float var = 0.0f;
  for (uint8_t i = 0; i < brWindowFill; i++) {
    float d = brWindow[i] - mean;
    var += d * d;
  }
  return sqrtf(var / brWindowFill);
}

// Sets flag after condition holds continuously for `ms` milliseconds.
static void debounce(bool condition, unsigned long now, unsigned long& since,
                     unsigned long ms, bool& flag) {
  if (condition) {
    if (!since) since = now;
    flag = (now - since >= ms);
  } else {
    since = 0;
    flag  = false;
  }
}

static bool prevPresence = false;

static void evaluateAlerts(unsigned long now) {
  // Declare timestamps first so they are in scope for the !presence reset below.
  static unsigned long zeroBreathSince = 0, lowBreathSince = 0, highBreathSince = 0;
  static unsigned long zeroHrSince     = 0, lowHrSince     = 0, highHrSince     = 0;

  if (presence != prevPresence) {
    fireAlert(presence ? EVT_PRESENCE_ON : EVT_PRESENCE_OFF, 0.0f);
    prevPresence = presence;
  }

  if (!presence) {
    alerts = {};
    prevAlerts = {};
    brWindowFill = brWindowIdx = 0;
    zeroBreathSince = lowBreathSince = highBreathSince = 0;
    zeroHrSince     = lowHrSince     = highHrSince     = 0;
    return;
  }

  debounce(breathingRate == 0.0f,                                now, zeroBreathSince, ALERT_ZERO_DEBOUNCE_MS, alerts.noBreathing);
  debounce(breathingRate > 0.0f && breathingRate < ALERT_BR_LOW, now, lowBreathSince,  ALERT_DEBOUNCE_MS,  alerts.lowBreathing);
  debounce(breathingRate > ALERT_BR_HIGH,                        now, highBreathSince, ALERT_DEBOUNCE_MS,  alerts.highBreathing);

  brWindow[brWindowIdx] = breathingRate;
  brWindowIdx = (brWindowIdx + 1) % ALERT_IRREG_WINDOW;
  if (brWindowFill < ALERT_IRREG_WINDOW) brWindowFill++;
  if (brWindowFill == ALERT_IRREG_WINDOW)
    alerts.irregularBreathing = (brStddev() > ALERT_BR_IRREG_STDDEV);

  debounce(heartRate == 0.0f,                              now, zeroHrSince, ALERT_ZERO_DEBOUNCE_MS, alerts.noHeartRate);
  debounce(heartRate > 0.0f && heartRate < ALERT_HR_LOW,  now, lowHrSince,  ALERT_DEBOUNCE_MS,  alerts.lowHeartRate);
  debounce(heartRate > ALERT_HR_HIGH,                     now, highHrSince,  ALERT_DEBOUNCE_MS,  alerts.highHeartRate);

#define FIRE_EDGE(field, event, val) \
  if (alerts.field != prevAlerts.field) fireAlert(event, val)

  FIRE_EDGE(noBreathing,        EVT_NO_BREATHING,        breathingRate);
  FIRE_EDGE(lowBreathing,       EVT_LOW_BREATHING,       breathingRate);
  FIRE_EDGE(highBreathing,      EVT_HIGH_BREATHING,      breathingRate);
  FIRE_EDGE(irregularBreathing, EVT_IRREGULAR_BREATHING, breathingRate);
  FIRE_EDGE(noHeartRate,        EVT_NO_HEART_RATE,       heartRate);
  FIRE_EDGE(lowHeartRate,       EVT_LOW_HEART_RATE,      heartRate);
  FIRE_EDGE(highHeartRate,      EVT_HIGH_HEART_RATE,     heartRate);

#undef FIRE_EDGE

  prevAlerts = alerts;
}

// ---------------------------------------------------------------------------
// Web server + WebSocket
// ---------------------------------------------------------------------------
#if ENABLE_WEBSERVER

static const char INDEX_HTML[] PROGMEM = R"html(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>mmWave</title>
  <style>
    *{box-sizing:border-box;margin:0;padding:0}
    body{background:#0f0f0f;color:#fff;font-family:-apple-system,BlinkMacSystemFont,sans-serif;
         display:flex;flex-direction:column;align-items:center;justify-content:center;
         min-height:100vh;gap:1.5rem}
    h1{font-size:.8rem;color:#555;text-transform:uppercase;letter-spacing:.2em}
    .row{display:flex;gap:1.5rem;flex-wrap:wrap;justify-content:center}
    .card{background:#1a1a1a;border:1px solid #252525;border-radius:16px;
          padding:2rem 2.8rem;text-align:center;min-width:145px}
    .label{font-size:.65rem;color:#555;text-transform:uppercase;letter-spacing:.14em;margin-bottom:.6rem}
    .val{font-size:3.8rem;font-weight:700;color:#3cf;line-height:1;font-variant-numeric:tabular-nums}
    .unit{font-size:.7rem;color:#444;margin-top:.5rem}
    footer{font-size:.65rem;color:#444;display:flex;align-items:center;gap:.4rem}
    #dot{width:7px;height:7px;border-radius:50%;background:#333}
    #dot.live{background:#3d3}
  </style>
</head>
<body>
  <h1>mmWave Monitor</h1>
  <div class="row">
    <div class="card">
      <div class="label">Breathing Rate</div>
      <div class="val" id="br">--</div>
      <div class="unit">rpm</div>
    </div>
    <div class="card">
      <div class="label">Heart Rate</div>
      <div class="val" id="hr">--</div>
      <div class="unit">bpm</div>
    </div>
  </div>
  <footer><div id="dot"></div><span id="st">connecting&hellip;</span></footer>
  <script>
    var ws, retries = 0;
    function connect() {
      ws = new WebSocket('ws://' + location.hostname + '/ws');
      ws.onopen = function() {
        document.getElementById('dot').className = 'live';
        document.getElementById('st').textContent = 'live';
        retries = 0;
      };
      ws.onmessage = function(e) {
        var d = JSON.parse(e.data);
        if (d.br != null) document.getElementById('br').textContent = d.br.toFixed(1);
        if (d.hr != null) document.getElementById('hr').textContent = d.hr.toFixed(1);
      };
      ws.onclose = function() {
        document.getElementById('dot').className = '';
        document.getElementById('st').textContent = 'reconnecting…';
        setTimeout(connect, Math.min(1000 * Math.pow(2, retries++), 10000));
      };
    }
    connect();
  </script>
</body>
</html>
)html";

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
               AwsEventType type, void*, uint8_t*, size_t) {
  if (type == WS_EVT_CONNECT)
    LOG("WS client #%u connected", client->id());
  else if (type == WS_EVT_DISCONNECT)
    LOG("WS client #%u disconnected", client->id());
}

#endif // ENABLE_WEBSERVER

// ---------------------------------------------------------------------------
// MQTT HA auto-discovery
// ---------------------------------------------------------------------------
#if ENABLE_MQTT

struct HaEntity {
  const char* entityType;
  const char* slug;
  const char* name;
  const char* valueTpl;
  const char* unit;        // NULL for binary_sensor
  const char* uniqueSuffix;
};

void publishEntity(const HaEntity& e) {
  char topic[128];
  char payload[384];
  snprintf(topic, sizeof(topic), "%s/%s/%s_%s/config",
           MQTT_HA_PREFIX, e.entityType, MQTT_TOPIC_PREFIX, e.slug);
  if (e.unit && e.unit[0]) {
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"state_topic\":\"%s/sensor/state\","
             "\"value_template\":\"%s\",\"unit_of_measurement\":\"%s\","
             "\"unique_id\":\"%s_%s\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\"}}",
             e.name, MQTT_TOPIC_PREFIX, e.valueTpl, e.unit,
             MQTT_TOPIC_PREFIX, e.uniqueSuffix, DEVICE_NAME, DEVICE_NAME);
  } else {
    snprintf(payload, sizeof(payload),
             "{\"name\":\"%s\",\"state_topic\":\"%s/sensor/state\","
             "\"value_template\":\"%s\","
             "\"unique_id\":\"%s_%s\","
             "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\"}}",
             e.name, MQTT_TOPIC_PREFIX, e.valueTpl,
             MQTT_TOPIC_PREFIX, e.uniqueSuffix, DEVICE_NAME, DEVICE_NAME);
  }
  mqttClient.publish(topic, payload, true);
}

void publishHaDiscovery() {
  static const HaEntity entities[] = {
    // Measurements
    {"sensor",        "breathing_rate",        "Breathing Rate",             "{{value_json.breathing_rate}}",        "rpm", "br"},
    {"sensor",        "heart_rate",            "Heart Rate",                 "{{value_json.heart_rate}}",            "bpm", "hr"},
    {"binary_sensor", "presence",              "Presence",                   "{{value_json.presence}}",              NULL,  "presence"},
    // Alerts
    {"binary_sensor", "alert_no_breathing",    "Alert: No Breathing",        "{{value_json.alert_no_breathing}}",    NULL,  "alert_no_br"},
    {"binary_sensor", "alert_low_breathing",   "Alert: Low Breathing",       "{{value_json.alert_low_breathing}}",   NULL,  "alert_low_br"},
    {"binary_sensor", "alert_high_breathing",  "Alert: High Breathing",      "{{value_json.alert_high_breathing}}",  NULL,  "alert_high_br"},
    {"binary_sensor", "alert_irreg_breathing", "Alert: Irregular Breathing", "{{value_json.alert_irreg_breathing}}", NULL,  "alert_irreg_br"},
    {"binary_sensor", "alert_no_hr",           "Alert: No Heart Rate",       "{{value_json.alert_no_hr}}",           NULL,  "alert_no_hr"},
    {"binary_sensor", "alert_low_hr",          "Alert: Low Heart Rate",      "{{value_json.alert_low_hr}}",          NULL,  "alert_low_hr"},
    {"binary_sensor", "alert_high_hr",         "Alert: High Heart Rate",     "{{value_json.alert_high_hr}}",         NULL,  "alert_high_hr"},
  };
  for (uint8_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
    publishEntity(entities[i]);
  }
  LOG("HA discovery published");
}

void mqttConnect() {
  if (mqttClient.connected()) return;
  LOG("Connecting to MQTT %s:%d", MQTT_HOST, MQTT_PORT);
  bool ok = (strlen(MQTT_USER) > 0)
    ? mqttClient.connect(DEVICE_NAME, MQTT_USER, MQTT_PASSWORD)
    : mqttClient.connect(DEVICE_NAME);
  if (ok) {
    LOG("MQTT connected");
    publishHaDiscovery();
  } else {
    LOG("MQTT failed rc=%d", mqttClient.state());
  }
}

#endif // ENABLE_MQTT

// ---------------------------------------------------------------------------
// WiFi — try all networks from config.h, fall back to captive portal
// ---------------------------------------------------------------------------
void connectWifi() {
  WiFiMulti wifiMulti;
  for (uint8_t i = 0; i < wifiNetworkCount; i++) {
    wifiMulti.addAP(wifiNetworks[i].ssid, wifiNetworks[i].pass);
    LOG("WiFi candidate %u: %s", i + 1, wifiNetworks[i].ssid);
  }
  if (wifiMulti.run(15000) == WL_CONNECTED) {
    LOG("WiFi connected — IP: %d.%d.%d.%d",
        WiFi.localIP()[0], WiFi.localIP()[1],
        WiFi.localIP()[2], WiFi.localIP()[3]);
    return;
  }
  LOG("WiFi failed, starting captive portal AP: %s", DEVICE_NAME);
  WiFiManager wm;
  wm.setConfigPortalTimeout(180);
  if (!wm.startConfigPortal(DEVICE_NAME, WIFI_AP_PASSWORD)) {
    LOG("Portal timed out, restarting");
    ESP.restart();
  }
}

// ---------------------------------------------------------------------------
// Alert callbacks
// ---------------------------------------------------------------------------
#if ENABLE_PUSHOVER
static Pushover _pushover(PUSHOVER_APP_TOKEN, PUSHOVER_USER_KEY);

// Deferred send: set by pushoverHandler(), consumed in loop() to avoid blocking sensor reads.
// WiFiClientSecure TLS handshake takes 1-3 s — must not run inside evaluateAlerts().
static bool _poPending  = false;
static char _poTitle[48];
static char _poMsg[80];
static int  _poPriority = 0;

static void pushoverHandler(AlertEvent event, float value) {
  int priority = (event == EVT_PRESENCE_ON || event == EVT_PRESENCE_OFF) ? 0 : 1;
  if (_poPending && priority <= _poPriority) return;
  snprintf(_poTitle, sizeof(_poTitle), "mmWave Alert");
  switch (event) {
    case EVT_NO_BREATHING:        snprintf(_poMsg, sizeof(_poMsg), "No breathing detected!");           break;
    case EVT_LOW_BREATHING:       snprintf(_poMsg, sizeof(_poMsg), "Low breathing: %.1f rpm", value);   break;
    case EVT_HIGH_BREATHING:      snprintf(_poMsg, sizeof(_poMsg), "High breathing: %.1f rpm", value);  break;
    case EVT_IRREGULAR_BREATHING: snprintf(_poMsg, sizeof(_poMsg), "Irregular breathing detected");     break;
    case EVT_NO_HEART_RATE:       snprintf(_poMsg, sizeof(_poMsg), "No heart rate detected!");          break;
    case EVT_LOW_HEART_RATE:      snprintf(_poMsg, sizeof(_poMsg), "Low heart rate: %.1f bpm", value);  break;
    case EVT_HIGH_HEART_RATE:     snprintf(_poMsg, sizeof(_poMsg), "High heart rate: %.1f bpm", value); break;
    case EVT_PRESENCE_ON:
    case EVT_PRESENCE_OFF:
      snprintf(_poTitle, sizeof(_poTitle), "mmWave");
      snprintf(_poMsg,   sizeof(_poMsg),   event == EVT_PRESENCE_ON ? "Presence detected" : "No presence"); break;
    default: return;
  }
  _poPending  = true;
  _poPriority = priority;
}
#endif // ENABLE_PUSHOVER

#if ENABLE_MQTT
static void mqttAlertHandler(AlertEvent event, float value) {
  if (!mqttClient.connected()) return;
  const char* evtName = NULL;
  switch (event) {
    case EVT_PRESENCE_ON:         evtName = "presence_on";         break;
    case EVT_PRESENCE_OFF:        evtName = "presence_off";        break;
    case EVT_NO_BREATHING:        evtName = "no_breathing";        break;
    case EVT_LOW_BREATHING:       evtName = "low_breathing";       break;
    case EVT_HIGH_BREATHING:      evtName = "high_breathing";      break;
    case EVT_IRREGULAR_BREATHING: evtName = "irregular_breathing"; break;
    case EVT_NO_HEART_RATE:       evtName = "no_heart_rate";       break;
    case EVT_LOW_HEART_RATE:      evtName = "low_heart_rate";      break;
    case EVT_HIGH_HEART_RATE:     evtName = "high_heart_rate";     break;
    default: return;
  }
  char topic[64], payload[64];
  snprintf(topic,   sizeof(topic),   "%s/alert", MQTT_TOPIC_PREFIX);
  snprintf(payload, sizeof(payload), "{\"event\":\"%s\",\"value\":%.1f}", evtName, value);
  mqttClient.publish(topic, payload);
  LOG("MQTT alert: %s", payload);
}
#endif // ENABLE_MQTT

// ---------------------------------------------------------------------------
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  LOG("mmWave booting");

  mmWave.begin(&mmWaveSerial);
  LOG("Sensor init OK");

  connectWifi();

  telnet.begin();
  LOG("Telnet started on port 23");

#if ENABLE_WEBSERVER || ENABLE_OTA
  if (!MDNS.begin(DEVICE_NAME)) {
    LOG("mDNS failed");
  } else {
    LOG("mDNS started: %s.local", DEVICE_NAME);
  }
#endif

#if ENABLE_OTA
  ArduinoOTA.setHostname(DEVICE_NAME);
  if (strlen(OTA_PASSWORD) > 0) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false); // we manage mDNS ourselves
  ArduinoOTA.onStart([]() {
    LOG("OTA update starting");
  });
  ArduinoOTA.onEnd([]() {
    LOG("OTA update done — rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    LOG("OTA %u%%", progress / (total / 100));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    const char* msg = (error == OTA_AUTH_ERROR)    ? "auth failed"    :
                      (error == OTA_BEGIN_ERROR)   ? "begin failed"   :
                      (error == OTA_CONNECT_ERROR) ? "connect failed" :
                      (error == OTA_RECEIVE_ERROR) ? "receive failed" :
                      (error == OTA_END_ERROR)     ? "end failed"     : "unknown";
    LOG("OTA error: %s", msg);
  });
  ArduinoOTA.begin();
  LOG("OTA ready");
#endif

#if ENABLE_WEBSERVER
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", INDEX_HTML);
  });
  server.begin();
  LOG("Web server started on port 80");
#endif

#if ENABLE_MQTT
  mqttClient.setBufferSize(512);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttConnect();
  onAlert(mqttAlertHandler);
#endif

#if ENABLE_PUSHOVER
  onAlert(pushoverHandler);
  LOG("Pushover alerts enabled (profile: %s)",
      VITAL_PROFILE == PROFILE_TODDLER ? "toddler" : "adult");
#if ALERT_NOTIFY_ONLINE
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    _poPending  = true;
    _poPriority = 0;
    snprintf(_poTitle, sizeof(_poTitle), "mmWave Online");
    snprintf(_poMsg,   sizeof(_poMsg),   "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  }
#endif
#endif
}

void loop() {
  telnet.loop();

#if ENABLE_OTA
  ArduinoOTA.handle();
#endif

  if (mmWave.update(0)) {
    float dist;
    mmWave.getBreathRate(breathingRate);
    mmWave.getHeartRate(heartRate);
    mmWave.getDistance(dist);
    presence = mmWave.isHumanDetected();
  }

  unsigned long now = millis();

  static unsigned long lastLog = 0;
  if (now - lastLog >= 1000UL) {
    lastLog = now;
    LOG("br=%.1f hr=%.1f presence=%d", breathingRate, heartRate, (int)presence);
    evaluateAlerts(now);
  }

#if ENABLE_PUSHOVER
  if (_poPending) {
    _poPending = false;
    int code = _pushover.send(_poTitle, _poMsg, _poPriority);
    LOG("Pushover send: %d", code);
  }
#endif

#if ENABLE_WEBSERVER
  if (now - lastWsPush >= 1000UL) {
    lastWsPush = now;
    if (ws.count() > 0) {
      char json[64];
      snprintf(json, sizeof(json), "{\"br\":%.1f,\"hr\":%.1f}", breathingRate, heartRate);
      ws.textAll(json);
    }
  }
  ws.cleanupClients();
#endif

#if ENABLE_MQTT
  if (now - lastMqttPub >= 5000UL) {
    lastMqttPub = now;
    if (!mqttClient.connected() && (now - lastMqttAttempt >= 30000UL)) {
      lastMqttAttempt = now;
      mqttConnect();
    }
    if (mqttClient.connected()) {
      char topic[64];
      char payload[256];
      snprintf(topic, sizeof(topic), "%s/sensor/state", MQTT_TOPIC_PREFIX);
      snprintf(payload, sizeof(payload),
               "{\"breathing_rate\":%.1f,\"heart_rate\":%.1f,\"presence\":\"%s\","
               "\"alert_no_breathing\":\"%s\",\"alert_low_breathing\":\"%s\","
               "\"alert_high_breathing\":\"%s\",\"alert_irreg_breathing\":\"%s\","
               "\"alert_no_hr\":\"%s\",\"alert_low_hr\":\"%s\",\"alert_high_hr\":\"%s\"}",
               breathingRate, heartRate, presence ? "ON" : "OFF",
               alerts.noBreathing        ? "ON" : "OFF",
               alerts.lowBreathing       ? "ON" : "OFF",
               alerts.highBreathing      ? "ON" : "OFF",
               alerts.irregularBreathing ? "ON" : "OFF",
               alerts.noHeartRate        ? "ON" : "OFF",
               alerts.lowHeartRate       ? "ON" : "OFF",
               alerts.highHeartRate      ? "ON" : "OFF");
      mqttClient.publish(topic, payload);
      LOG("MQTT state: %s", payload);
    }
  }
  mqttClient.loop();
#endif
}
