#include "Pushover.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

static const char PO_URL[] = "https://api.pushover.net/1/messages.json";

Pushover::Pushover(const char* appToken, const char* userKey)
  : _token(appToken), _user(userKey) {}

int Pushover::send(const char* title, const char* message, int priority) {
  WiFiClientSecure client;
  client.setInsecure();   // cert pinning unnecessary for push notifications
  HTTPClient http;
  if (!http.begin(client, PO_URL)) return -1;
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  char body[384];
  snprintf(body, sizeof(body),
           "token=%s&user=%s&title=%s&message=%s&priority=%d",
           _token, _user, title, message, priority);
  int code = http.POST(reinterpret_cast<uint8_t*>(body), strlen(body));
  http.end();
  return code;
}
