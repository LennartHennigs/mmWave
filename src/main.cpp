#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiManager.h>
#include "config.h"
#include "mmWaveKit.h"
#if ENABLE_TELNET
  #include <ESPTelnet.h>
#endif

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


///////////////////////////////////////////////////////////////////////////////
// Logging — Serial if DEBUG; Telnet if ENABLE_TELNET
///////////////////////////////////////////////////////////////////////////////
#if ENABLE_TELNET
ESPTelnet telnet;
#endif

static void logPrint(const char* fmt, ...) {
#if !DEBUG
  #if ENABLE_TELNET
    if (!telnet.isConnected()) return;
  #else
    return;
  #endif
#endif
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
#if DEBUG
  Serial.print(buf);
#endif
#if ENABLE_TELNET
  telnet.print(buf);
#endif
}
#define LOG(fmt, ...) logPrint("[DBG] " fmt "\n", ##__VA_ARGS__)

static mmWaveKit kit;
static const char* const profileName = VITAL_PROFILE == PROFILE_TODDLER ? "toddler"
                                     : VITAL_PROFILE == PROFILE_CHILD   ? "child"
                                     : "adult";

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

struct WifiCred { const char* ssid; const char* pass; };
static const WifiCred wifiNetworks[] = { WIFI_NETWORKS };
static const uint8_t  wifiNetworkCount = sizeof(wifiNetworks) / sizeof(wifiNetworks[0]);


///////////////////////////////////////////////////////////////////////////////
// Web server + WebSocket
///////////////////////////////////////////////////////////////////////////////
#if ENABLE_WEBSERVER

#include "index_html.h"

void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
               AwsEventType type, void*, uint8_t*, size_t) {
  if (type == WS_EVT_CONNECT)
    LOG("WS client #%u connected", client->id());
  else if (type == WS_EVT_DISCONNECT)
    LOG("WS client #%u disconnected", client->id());
}

#endif // ENABLE_WEBSERVER

///////////////////////////////////////////////////////////////////////////////
// MQTT HA auto-discovery
///////////////////////////////////////////////////////////////////////////////
#if ENABLE_MQTT

struct HaEntity {
  const char* entityType;
  const char* slug;
  const char* name;
  const char* valueTpl;
  const char* unit;
  const char* uniqueSuffix;
};

void publishEntity(const HaEntity& e) {
  char topic[128];
  char payload[384];
  snprintf(topic, sizeof(topic), "%s/%s/%s_%s/config",
           MQTT_HA_PREFIX, e.entityType, MQTT_TOPIC_PREFIX, e.slug);
  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"state_topic\":\"%s/sensor/state\","
           "\"value_template\":\"%s\",\"unit_of_measurement\":\"%s\","
           "\"unique_id\":\"%s_%s\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\"}}",
           e.name, MQTT_TOPIC_PREFIX, e.valueTpl, e.unit,
           MQTT_TOPIC_PREFIX, e.uniqueSuffix, DEVICE_NAME, DEVICE_NAME);
  mqttClient.publish(topic, payload, true);
}

void publishHaDiscovery() {
  static const HaEntity entities[] = {
    {"sensor",        "breathing_rate",        "Breathing Rate",             "{{value_json.breathing_rate}}",        "rpm", "br"},
    {"sensor",        "heart_rate",            "Heart Rate",                 "{{value_json.heart_rate}}",            "bpm", "hr"},
    {"binary_sensor", "presence",              "Presence",                   "{{value_json.presence}}",              "",    "presence"},
    {"binary_sensor", "alert_no_breathing",    "Alert: No Breathing",        "{{value_json.alert_no_breathing}}",    "",    "alert_no_br"},
    {"binary_sensor", "alert_low_breathing",   "Alert: Low Breathing",       "{{value_json.alert_low_breathing}}",   "",    "alert_low_br"},
    {"binary_sensor", "alert_high_breathing",  "Alert: High Breathing",      "{{value_json.alert_high_breathing}}",  "",    "alert_high_br"},
    {"binary_sensor", "alert_irreg_breathing", "Alert: Irregular Breathing", "{{value_json.alert_irreg_breathing}}", "",    "alert_irreg_br"},
    {"binary_sensor", "alert_no_hr",           "Alert: No Heart Rate",       "{{value_json.alert_no_hr}}",           "",    "alert_no_hr"},
    {"binary_sensor", "alert_low_hr",          "Alert: Low Heart Rate",      "{{value_json.alert_low_hr}}",          "",    "alert_low_hr"},
    {"binary_sensor", "alert_high_hr",         "Alert: High Heart Rate",     "{{value_json.alert_high_hr}}",         "",    "alert_high_hr"},
    {"sensor",        "light_lux",             "Light Level",                "{{value_json.light_lux}}",             "lx",  "light_lux"},
    {"sensor",        "distance",              "Detection Distance",          "{{value_json.distance}}",              "cm",  "distance"},
  };
  for (uint8_t i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
    publishEntity(entities[i]);
  }
  LOG("HA discovery published");
}

void mqttConnect() {
  if (mqttClient.connected()) return;
  LOG("Connecting to MQTT %s:%d", MQTT_HOST, MQTT_PORT);
  bool ok = MQTT_USER[0]
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

///////////////////////////////////////////////////////////////////////////////
// WiFi — try all networks from config.h, fall back to captive portal
///////////////////////////////////////////////////////////////////////////////
void connectWifi() {
  WiFiMulti wifiMulti;
  for (uint8_t i = 0; i < wifiNetworkCount; i++) {
    wifiMulti.addAP(wifiNetworks[i].ssid, wifiNetworks[i].pass);
    LOG("WiFi candidate %u: %s", i + 1, wifiNetworks[i].ssid);
  }
  if (wifiMulti.run(15000) == WL_CONNECTED) {
    IPAddress ip = WiFi.localIP();
    LOG("WiFi connected — IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
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

///////////////////////////////////////////////////////////////////////////////
// Alert callbacks — gate checks + Pushover/MQTT wiring
///////////////////////////////////////////////////////////////////////////////
#if ENABLE_PUSHOVER || ENABLE_MQTT
static bool shouldNotify(mmWaveKit::Event e) {
  switch (e) {
    case mmWaveKit::EVT_PRESENCE_ON:             return ALERT_NOTIFY_PRESENCE_ON;
    case mmWaveKit::EVT_PRESENCE_OFF:            return ALERT_NOTIFY_PRESENCE_OFF;
    case mmWaveKit::EVT_NO_BREATHING:
    case mmWaveKit::EVT_LOW_BREATHING:
    case mmWaveKit::EVT_HIGH_BREATHING:
    case mmWaveKit::EVT_IRREGULAR_BREATHING:     return ALERT_NOTIFY_BREATHING;
    case mmWaveKit::EVT_NO_HEART_RATE:
    case mmWaveKit::EVT_LOW_HEART_RATE:
    case mmWaveKit::EVT_HIGH_HEART_RATE:         return ALERT_NOTIFY_HEART_RATE;
    default:                                     return false;
  }
}
#endif

#if ENABLE_PUSHOVER
static Pushover _pushover(PUSHOVER_APP_TOKEN, PUSHOVER_USER_KEY);

static bool _poPending  = false;
static char _poTitle[48];
static char _poMsg[80];
static int  _poPriority = 0;

static void pushoverHandler(mmWaveKit::Event e, int value) {
  if (!shouldNotify(e)) return;
  int priority = (e == mmWaveKit::EVT_PRESENCE_ON || e == mmWaveKit::EVT_PRESENCE_OFF) ? 0 : 1;
  if (_poPending && priority <= _poPriority) return;
  switch (e) {
    case mmWaveKit::EVT_NO_BREATHING:        snprintf(_poMsg, sizeof(_poMsg), "No breathing detected!");          break;
    case mmWaveKit::EVT_LOW_BREATHING:       snprintf(_poMsg, sizeof(_poMsg), "Low breathing: %d rpm", value);    break;
    case mmWaveKit::EVT_HIGH_BREATHING:      snprintf(_poMsg, sizeof(_poMsg), "High breathing: %d rpm", value);   break;
    case mmWaveKit::EVT_IRREGULAR_BREATHING: snprintf(_poMsg, sizeof(_poMsg), "Irregular breathing detected");    break;
    case mmWaveKit::EVT_NO_HEART_RATE:       snprintf(_poMsg, sizeof(_poMsg), "No heart rate detected!");         break;
    case mmWaveKit::EVT_LOW_HEART_RATE:      snprintf(_poMsg, sizeof(_poMsg), "Low heart rate: %d bpm", value);  break;
    case mmWaveKit::EVT_HIGH_HEART_RATE:     snprintf(_poMsg, sizeof(_poMsg), "High heart rate: %d bpm", value); break;
    case mmWaveKit::EVT_PRESENCE_ON:         snprintf(_poMsg, sizeof(_poMsg), "Presence detected");              break;
    case mmWaveKit::EVT_PRESENCE_OFF:        snprintf(_poMsg, sizeof(_poMsg), "No presence");                    break;
    default: return;
  }
  snprintf(_poTitle, sizeof(_poTitle), priority == 1 ? "mmWave Alert" : "mmWave");
  _poPending  = true;
  _poPriority = priority;
}
#endif // ENABLE_PUSHOVER

#if ENABLE_MQTT
static void mqttAlertHandler(mmWaveKit::Event e, int value) {
  if (!mqttClient.connected()) return;
  if (!shouldNotify(e)) return;
  const char* evtName = NULL;
  switch (e) {
    case mmWaveKit::EVT_PRESENCE_ON:         evtName = "presence_on";         break;
    case mmWaveKit::EVT_PRESENCE_OFF:        evtName = "presence_off";        break;
    case mmWaveKit::EVT_NO_BREATHING:        evtName = "no_breathing";        break;
    case mmWaveKit::EVT_LOW_BREATHING:       evtName = "low_breathing";       break;
    case mmWaveKit::EVT_HIGH_BREATHING:      evtName = "high_breathing";      break;
    case mmWaveKit::EVT_IRREGULAR_BREATHING: evtName = "irregular_breathing"; break;
    case mmWaveKit::EVT_NO_HEART_RATE:       evtName = "no_heart_rate";       break;
    case mmWaveKit::EVT_LOW_HEART_RATE:      evtName = "low_heart_rate";      break;
    case mmWaveKit::EVT_HIGH_HEART_RATE:     evtName = "high_heart_rate";     break;
    default: return;
  }
  char topic[64], payload[64];
  snprintf(topic,   sizeof(topic),   "%s/alert", MQTT_TOPIC_PREFIX);
  snprintf(payload, sizeof(payload), "{\"event\":\"%s\",\"value\":%d}", evtName, value);
  mqttClient.publish(topic, payload);
  LOG("MQTT alert: %s", payload);
}
#endif // ENABLE_MQTT

///////////////////////////////////////////////////////////////////////////////
// Setup helpers
///////////////////////////////////////////////////////////////////////////////

void setupSensor() {
  mmWaveKit::VitalConfig vc;
  vc.profile = VITAL_PROFILE == PROFILE_TODDLER ? mmWaveKit::TODDLER
             : VITAL_PROFILE == PROFILE_CHILD   ? mmWaveKit::CHILD
             : mmWaveKit::ADULT;

#if ENABLE_PUSHOVER || ENABLE_MQTT
  kit.onEvent([](mmWaveKit&, mmWaveKit::Event e, int v) {
#if ENABLE_PUSHOVER
    pushoverHandler(e, v);
#endif
#if ENABLE_MQTT
    mqttAlertHandler(e, v);
#endif
  });
#endif
  kit.onBecameLight([](mmWaveKit&, mmWaveKit::Event, int lux) { LOG("light on (%d lux)", lux); });
  kit.onBecameDark ([](mmWaveKit&, mmWaveKit::Event, int lux) { LOG("light off (%d lux)", lux); });

  if (!kit.begin(vc, {}))
    LOG("mmWaveKit: light sensor init failed");
  else
    LOG("mmWaveKit ready (profile: %s)", profileName);
}

void setupTelnet() {
#if ENABLE_TELNET
  telnet.begin();
  LOG("Telnet started on port 23");
#endif
}

void setupMdns() {
#if ENABLE_WEBSERVER || ENABLE_OTA
  if (!MDNS.begin(DEVICE_NAME))
    LOG("mDNS failed");
  else
    LOG("mDNS started: %s.local", DEVICE_NAME);
#endif
}

void setupOTA() {
#if ENABLE_OTA
  ArduinoOTA.setHostname(DEVICE_NAME);
  if (OTA_PASSWORD[0]) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.setMdnsEnabled(false);
  ArduinoOTA.onStart([]()   { LOG("OTA update starting"); });
  ArduinoOTA.onEnd([]()     { LOG("OTA update done — rebooting"); });
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
}

void setupWebServer() {
#if ENABLE_WEBSERVER
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(200, "text/html", INDEX_HTML);
  });
  server.on("/info", HTTP_GET, [](AsyncWebServerRequest* req) {
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"track\":%d,\"threshold\":%d,\"profile\":\"%s\"}",
             LIGHT_TRACK_MODE, kit.getThreshold(), profileName);
    req->send(200, "application/json", buf);
  });
  server.begin();
  LOG("Web server started on port 80");
#endif
}

void setupMqtt() {
#if ENABLE_MQTT
  mqttClient.setBufferSize(512);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttConnect();
#endif
}

void setupPushover() {
#if ENABLE_PUSHOVER
  LOG("Pushover alerts enabled (profile: %s)", profileName);
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

///////////////////////////////////////////////////////////////////////////////
// Loop helpers
///////////////////////////////////////////////////////////////////////////////

void loopDebugLog(unsigned long now) {
  static unsigned long lastLog = 0;
  if (now - lastLog < 1000UL) return;
  lastLog = now;
  LOG("br=%d hr=%d presence=%d lux=%d dist=%.1f track=%s",
      kit.getBreathingRate(), kit.getHeartRate(),
      (int)kit.isPresent(), kit.getLux(), kit.getDistance(),
      kit.isTrackingActive() ? "active" : "gated");
}

void loopPushover() {
#if ENABLE_PUSHOVER
  if (_poPending) {
    _poPending = false;
    int code = _pushover.send(_poTitle, _poMsg, _poPriority,
                              "http://" DEVICE_NAME ".local", "Open Dashboard");
    LOG("Pushover send: %d", code);
  }
#endif
}

void loopWebServer(unsigned long now) {
#if ENABLE_WEBSERVER
  if (now - lastWsPush >= 1000UL) {
    lastWsPush = now;
    if (ws.count() > 0) {
      char json[128];
      snprintf(json, sizeof(json),
               "{\"br\":%d,\"hr\":%d,\"presence\":%s,\"lx\":%d,\"dist\":%.1f}",
               kit.getBreathingRate(), kit.getHeartRate(),
               kit.isPresent() ? "true" : "false", kit.getLux(),
               kit.getDistance());
      ws.textAll(json);
    }
  }
  ws.cleanupClients();
#endif
}

void loopMqtt(unsigned long now) {
#if ENABLE_MQTT
  if (!mqttClient.connected() && (now - lastMqttAttempt >= 30000UL)) {
    lastMqttAttempt = now;
    mqttConnect();
  }
  if (mqttClient.connected() && now - lastMqttPub >= 5000UL) {
    lastMqttPub = now;
    if (kit.isTrackingActive()) {
      const mmWaveKit::AlertState& a = kit.getAlerts();
      char topic[64];
      char payload[320];
      snprintf(topic, sizeof(topic), "%s/sensor/state", MQTT_TOPIC_PREFIX);
      snprintf(payload, sizeof(payload),
               "{\"breathing_rate\":%d,\"heart_rate\":%d,\"presence\":\"%s\","
               "\"alert_no_breathing\":\"%s\",\"alert_low_breathing\":\"%s\","
               "\"alert_high_breathing\":\"%s\",\"alert_irreg_breathing\":\"%s\","
               "\"alert_no_hr\":\"%s\",\"alert_low_hr\":\"%s\",\"alert_high_hr\":\"%s\","
               "\"light_lux\":%d,\"distance\":%.1f}",
               kit.getBreathingRate(), kit.getHeartRate(),
               kit.isPresent() ? "ON" : "OFF",
               a.noBreathing        ? "ON" : "OFF",
               a.lowBreathing       ? "ON" : "OFF",
               a.highBreathing      ? "ON" : "OFF",
               a.irregularBreathing ? "ON" : "OFF",
               a.noHeartRate        ? "ON" : "OFF",
               a.lowHeartRate       ? "ON" : "OFF",
               a.highHeartRate      ? "ON" : "OFF",
               kit.getLux(), kit.getDistance());
      mqttClient.publish(topic, payload);
      LOG("MQTT state: %s", payload);
    }
  }
  mqttClient.loop();
#endif
}

///////////////////////////////////////////////////////////////////////////////
// Setup & loop
///////////////////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(115200);
  LOG("mmWave booting");
  setupSensor();
  connectWifi();
  setupTelnet();
  setupMdns();
  setupOTA();
  setupWebServer();
  setupMqtt();
  setupPushover();
}

void loop() {
#if ENABLE_TELNET
  telnet.loop();
#endif
#if ENABLE_OTA
  ArduinoOTA.handle();
#endif
  kit.update();
  unsigned long now = millis();
  loopDebugLog(now);
  loopPushover();
  loopWebServer(now);
  loopMqtt(now);
}
