/////////////////////////////////////////////////////////////////
/*
  Pushover.h - Minimal Pushover push notification client for ESP32.
  Copyright (C) 2026 Lennart Hennigs.
  Released under the MIT license.
*/
/////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>

// Minimal Pushover client for ESP32.
// Uses built-in HTTPClient + WiFiClientSecure — no extra dependencies.
//
// Usage:
//   Pushover po(PUSHOVER_APP_TOKEN, PUSHOVER_USER_KEY);
//   po.send("Title", "Message body");          // normal priority
//   po.send("Alert", "No breathing!", 1);      // priority 1 = high, bypasses quiet hours
//
// Priority values:
//   -1  quiet   (no sound/vibration)
//    0  normal  (default)
//    1  high    (bypasses user quiet hours)
//    2  emergency (requires acknowledgement — needs retry/expire params, not supported here)
//
// Returns the HTTP status code (200 = success), or -1 if the connection failed.

class Pushover {
public:
  Pushover(const char* appToken, const char* userKey);

  int send(const char* title, const char* message, int priority = 0);

private:
  const char* _token;
  const char* _user;
};
