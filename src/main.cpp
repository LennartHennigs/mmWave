#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HardwareSerial.h>
#include "Seeed_Arduino_mmWave.h"
#include "config.h"

#if ENABLE_WEBSERVER
  #include <ESPmDNS.h>
  #include <AsyncTCP.h>
  #include <ESPAsyncWebServer.h>
#endif

#if ENABLE_MQTT
  #include <PubSubClient.h>
#endif

#if DEBUG
  #define LOG(fmt, ...) Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
#else
  #define LOG(...) do {} while(0)
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
    {"sensor",        "breathing_rate", "Breathing Rate", "{{value_json.breathing_rate}}", "rpm", "br"},
    {"sensor",        "heart_rate",     "Heart Rate",     "{{value_json.heart_rate}}",     "bpm", "hr"},
    {"binary_sensor", "presence",       "Presence",       "{{value_json.presence}}",       NULL,  "presence"},
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
// WiFi — try config.h creds, fall back to captive portal
// ---------------------------------------------------------------------------
void connectWifi() {
  LOG("Connecting to WiFi: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long deadline = millis() + 10000UL;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(200);
  }
  if (WiFi.status() == WL_CONNECTED) {
    LOG("WiFi connected");
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
// Setup & loop
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  LOG("mmWave booting");

  mmWave.begin(&mmWaveSerial);
  LOG("Sensor init OK");

  connectWifi();

#if ENABLE_WEBSERVER
  if (!MDNS.begin(DEVICE_NAME)) {
    LOG("mDNS failed");
  } else {
    LOG("mDNS started: http://%s.local", DEVICE_NAME);
  }

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/html", INDEX_HTML);
  });
  server.begin();
  LOG("Web server started on port 80");
#endif

#if ENABLE_MQTT
  mqttClient.setBufferSize(512);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttConnect();
#endif
}

void loop() {
  if (mmWave.update(0)) {
    float dist;
    mmWave.getBreathRate(breathingRate);
    mmWave.getHeartRate(heartRate);
    mmWave.getDistance(dist);
    presence = mmWave.isHumanDetected();
    LOG("br=%.1f hr=%.1f presence=%d", breathingRate, heartRate, (int)presence);
  }

  unsigned long now = millis();

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
      char payload[96];
      snprintf(topic, sizeof(topic), "%s/sensor/state", MQTT_TOPIC_PREFIX);
      snprintf(payload, sizeof(payload),
               "{\"breathing_rate\":%.1f,\"heart_rate\":%.1f,\"presence\":\"%s\"}",
               breathingRate, heartRate, presence ? "ON" : "OFF");
      mqttClient.publish(topic, payload);
      LOG("MQTT state: %s", payload);
    }
  }
  mqttClient.loop();
#endif
}
