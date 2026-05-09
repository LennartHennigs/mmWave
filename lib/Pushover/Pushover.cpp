/////////////////////////////////////////////////////////////////
/*
  Pushover.cpp - Minimal Pushover push notification client for ESP32.
  Copyright (C) 2026 Lennart Hennigs.
  Released under the MIT license.
*/
/////////////////////////////////////////////////////////////////

#include "Pushover.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const char PO_URL[] = "https://api.pushover.net/1/messages.json";

Pushover::Pushover(const char* appToken, const char* userKey)
  : _token(appToken), _user(userKey) {}

int Pushover::send(const char* title, const char* message, int priority,
                   const char* url, const char* urlTitle) {
  WiFiClientSecure client;
  client.setInsecure();   // cert pinning unnecessary for push notifications
  HTTPClient http;
  if (!http.begin(client, PO_URL)) return -1;
  http.addHeader("Content-Type", "application/json");
  // NOTE: title, message, url, urlTitle must not contain '"' or '\\'.
  // All values produced by this firmware are plain ASCII, so raw JSON embedding is safe.
  char body[512];
  int len = snprintf(body, sizeof(body),
                     "{\"token\":\"%s\",\"user\":\"%s\","
                     "\"title\":\"%s\",\"message\":\"%s\",\"priority\":%d",
                     _token, _user, title, message, priority);
  if (url && url[0] && len < (int)sizeof(body) - 1)
    len += snprintf(body + len, sizeof(body) - len, ",\"url\":\"%s\"", url);
  if (urlTitle && urlTitle[0] && len < (int)sizeof(body) - 1)
    len += snprintf(body + len, sizeof(body) - len, ",\"url_title\":\"%s\"", urlTitle);
  if (len < (int)sizeof(body) - 1)
    snprintf(body + len, sizeof(body) - len, "}");
  int code = http.POST(body);
  http.end();
  return code;
}
