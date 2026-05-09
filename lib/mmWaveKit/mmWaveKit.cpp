/////////////////////////////////////////////////////////////////
/*
  mmWaveKit.cpp - Arduino library for the Seeed MR60BHA2 mmWave sensor.
  Copyright (C) 2026 Lennart Hennigs.
  Released under the MIT license.
*/
/////////////////////////////////////////////////////////////////

#include "mmWaveKit.h"
#include "Seeed_Arduino_mmWave.h"
#include <Wire.h>
#include <BH1750.h>
#include <Adafruit_NeoPixel.h>

static BH1750            _bh;
// Pin overridden at begin() via _led.setPin(lc.ledPin); 1 is the boot default.
static Adafruit_NeoPixel _led(1, 1, NEO_GRB + NEO_KHZ800);
static HardwareSerial    _serial(0);
static SEEED_MR60BHA2    _mmwave;

const mmWaveKit::VitalProfile mmWaveKit::ADULT   = { 10, 20, 3.0f,  40, 100 };
const mmWaveKit::VitalProfile mmWaveKit::CHILD   = { 16, 30, 4.0f,  60, 120 };
const mmWaveKit::VitalProfile mmWaveKit::TODDLER = { 16, 45, 5.0f,  60, 160 };

///////////////////////////////////////////////////////////////////////////////
bool mmWaveKit::begin(const VitalConfig& vc, const LightConfig& lc) {
  return begin(_serial, vc, lc);
}

bool mmWaveKit::begin(HardwareSerial& serial,
                      const VitalConfig& vc, const LightConfig& lc) {
  _vitalCfg = vc;
  _lightCfg = lc;

  _mmwave.begin(&serial);
  _requestFirmwareVersion();

  Wire.begin();
  bool ok = _bh.begin(BH1750::CONTINUOUS_HIGH_RES_MODE);

  _led.setPin(_lightCfg.ledPin);
  _led.begin();
  _led.setBrightness(40);
  _led.clear();
  _led.show();

  return ok;
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_requestFirmwareVersion() {
  _mmwave.send(0xFFFF);
}

bool mmWaveKit::getFirmwareVersion(uint8_t& major, uint8_t& sub, uint8_t& modified) const {
  if (!_fwValid) return false;
  major    = _fwMajor;
  sub      = _fwSub;
  modified = _fwModified;
  return true;
}

bool mmWaveKit::getFirmwareVersion(char* buf, size_t len) const {
  if (!_fwValid || !buf || len == 0) return false;
  snprintf(buf, len, "%u.%u.%u", _fwMajor, _fwSub, _fwModified);
  return true;
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::update() {
  bool anyDetected = false;
  while (_mmwave.update(0)) {
    // getBreathRate/getHeartRate are consume-once latches: they only update
    // the value when a new frame of that type arrives, leaving it unchanged otherwise.
    // Do not reset _br/_hr before the loop — BR/HR frames arrive less frequently
    // than distance/presence frames, so most calls would otherwise see zero.
    _mmwave.getBreathRate(_br);
    _mmwave.getHeartRate(_hr);
    float dist;
    if (_mmwave.getDistance(dist)) { _dist = dist; anyDetected = true; }
    if (_mmwave.isHumanDetected())  anyDetected = true;
    if (!_fwValid) {
      FirmwareInfo fi;
      if (_mmwave.getFirmwareInfo(fi)) {
        _fwMajor    = fi.firmware_verson.major_version;
        _fwSub      = fi.firmware_verson.sub_version;
        _fwModified = fi.firmware_verson.modified_version;
        _fwValid    = true;
      }
    }
  }
  _presence = anyDetected || _br > 0.0f || _hr > 0.0f;

  if (_bh.measurementReady()) _lux = _bh.readLightLevel();

  unsigned long now = millis();
  if (now - _lastEval >= 1000UL) {
    _lastEval = now;
    _evalVitals(now);
    _evalLight();
  }
}

///////////////////////////////////////////////////////////////////////////////
bool mmWaveKit::isTrackingActive() const {
  if (_lightCfg.trackMode == LIGHT_TRACK_DARK)  return (int)_lux <  _lightCfg.threshold;
  if (_lightCfg.trackMode == LIGHT_TRACK_LIGHT) return (int)_lux >= _lightCfg.threshold;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_fire(Event e, int value) {
  if (_onEvent) _onEvent(*this, e, value);

  EventCallback specific = nullptr;
  switch (e) {
    case EVT_PRESENCE_ON:         specific = _onPresenceOn;         break;
    case EVT_PRESENCE_OFF:        specific = _onPresenceOff;        break;
    case EVT_NO_BREATHING:        specific = _onNoBreathing;        break;
    case EVT_LOW_BREATHING:       specific = _onBreathingLow;       break;
    case EVT_HIGH_BREATHING:      specific = _onBreathingHigh;      break;
    case EVT_IRREGULAR_BREATHING: specific = _onBreathingIrregular; break;
    case EVT_NO_HEART_RATE:       specific = _onNoHeartRate;        break;
    case EVT_LOW_HEART_RATE:      specific = _onHeartRateLow;       break;
    case EVT_HIGH_HEART_RATE:     specific = _onHeartRateHigh;      break;
    case EVT_BECAME_LIGHT:        specific = _onBecameLight;        break;
    case EVT_BECAME_DARK:         specific = _onBecameDark;         break;
  }
  if (specific) specific(*this, e, value);

  if ((e == EVT_BECAME_LIGHT || e == EVT_BECAME_DARK) && _onLightSituationChanged)
    _onLightSituationChanged(*this, e, value);
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_fireEdge(bool cur, bool prev, Event e, int val) {
  if (cur != prev) _fire(e, val);
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_debounce(bool cond, unsigned long now, unsigned long& since,
                          uint32_t ms, bool& flag) {
  if (cond) {
    if (!since) since = now;
    flag = (now - since >= ms);
  } else {
    since = 0;
    flag  = false;
  }
}

///////////////////////////////////////////////////////////////////////////////
float mmWaveKit::_brStddev() {
  if (_brWindowFill < 2) return 0.0f;
  float mean = 0.0f;
  for (uint8_t i = 0; i < _brWindowFill; i++) mean += _brWindow[i];
  mean /= _brWindowFill;
  float var = 0.0f;
  for (uint8_t i = 0; i < _brWindowFill; i++) {
    float d = _brWindow[i] - mean;
    var += d * d;
  }
  return sqrtf(var / (float)(_brWindowFill - 1));  // sample stddev (Bessel's correction)
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_evalPresence() {
  if (_presence != _prevPresence) {
    _fire(_presence ? EVT_PRESENCE_ON : EVT_PRESENCE_OFF, 0);
    _prevPresence = _presence;
  }
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_evalBreathing(unsigned long now) {
  const VitalProfile& p = _vitalCfg.profile;
  const int br = (int)_br;
  _debounce(_br == 0.0f,                now, _zeroBreathSince, _vitalCfg.zeroDebounceMs,   _alerts.noBreathing);
  _debounce(_br > 0.0f && br < p.brLow, now, _lowBreathSince,  _vitalCfg.threshDebounceMs, _alerts.lowBreathing);
  _debounce(br > p.brHigh,              now, _highBreathSince, _vitalCfg.threshDebounceMs, _alerts.highBreathing);
  _brWindow[_brWindowIdx] = _br;
  _brWindowIdx = (_brWindowIdx + 1) % IRREG_WINDOW;
  if (_brWindowFill < IRREG_WINDOW) _brWindowFill++;
  if (_brWindowFill == IRREG_WINDOW)
    _alerts.irregularBreathing = (_brStddev() > p.brIrregStddev);
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_evalHeartRate(unsigned long now) {
  const VitalProfile& p = _vitalCfg.profile;
  const int hr = (int)_hr;
  _debounce(_hr == 0.0f,                now, _zeroHrSince, _vitalCfg.zeroDebounceMs,   _alerts.noHeartRate);
  _debounce(_hr > 0.0f && hr < p.hrLow, now, _lowHrSince,  _vitalCfg.threshDebounceMs, _alerts.lowHeartRate);
  _debounce(hr > p.hrHigh,              now, _highHrSince, _vitalCfg.threshDebounceMs, _alerts.highHeartRate);
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_evalVitals(unsigned long now) {
  _evalPresence();

  if (!_presence) {
    _alerts = {};  _prevAlerts = {};
    _brWindowFill = _brWindowIdx = 0;
    _zeroBreathSince = _lowBreathSince = _highBreathSince = 0;
    _zeroHrSince     = _lowHrSince     = _highHrSince     = 0;
    return;
  }

  _evalBreathing(now);
  _evalHeartRate(now);

  const int br = (int)_br;
  const int hr = (int)_hr;

  _fireEdge(_alerts.noBreathing,        _prevAlerts.noBreathing,        EVT_NO_BREATHING,        br);
  _fireEdge(_alerts.lowBreathing,       _prevAlerts.lowBreathing,       EVT_LOW_BREATHING,       br);
  _fireEdge(_alerts.highBreathing,      _prevAlerts.highBreathing,      EVT_HIGH_BREATHING,      br);
  _fireEdge(_alerts.irregularBreathing, _prevAlerts.irregularBreathing, EVT_IRREGULAR_BREATHING, br);
  _fireEdge(_alerts.noHeartRate,        _prevAlerts.noHeartRate,        EVT_NO_HEART_RATE,       hr);
  _fireEdge(_alerts.lowHeartRate,       _prevAlerts.lowHeartRate,       EVT_LOW_HEART_RATE,      hr);
  _fireEdge(_alerts.highHeartRate,      _prevAlerts.highHeartRate,      EVT_HIGH_HEART_RATE,     hr);

  _prevAlerts = _alerts;
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::_evalLight() {
  bool nowLight = ((int)_lux >= _lightCfg.threshold);
  if (nowLight != _prevLight) {
    _fire(nowLight ? EVT_BECAME_LIGHT : EVT_BECAME_DARK, getLux());
    _prevLight = nowLight;
  }
}

///////////////////////////////////////////////////////////////////////////////
void mmWaveKit::setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  _led.setPixelColor(0, _led.Color(r, g, b));
  _led.show();
}

void mmWaveKit::setLedOff() { setLedColor(0, 0, 0); }
