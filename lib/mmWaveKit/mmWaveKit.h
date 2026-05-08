/////////////////////////////////////////////////////////////////
/*
  mmWaveKit.h - Arduino library for the Seeed MR60BHA2 mmWave sensor.
  Copyright (C) 2026 Lennart Hennigs.
  Released under the MIT license.
*/
/////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>

#ifndef LIGHT_TRACK_ALWAYS
  #define LIGHT_TRACK_ALWAYS  1
  #define LIGHT_TRACK_DARK    2
  #define LIGHT_TRACK_LIGHT   3
#endif
#ifndef LIGHT_TRACK_MODE
  #define LIGHT_TRACK_MODE  LIGHT_TRACK_ALWAYS
#endif

#ifndef PROFILE_ADULT
  #define PROFILE_ADULT   0
  #define PROFILE_TODDLER 1
#endif


class mmWaveKit {
public:

  // ── Profiles — two preset instances of the same struct ───────────────
  struct VitalProfile {
    int   brLow, brHigh;      // rpm thresholds
    float brIrregStddev;      // rpm std-dev threshold for irregular breathing
    int   hrLow, hrHigh;      // bpm thresholds
  };
  static const VitalProfile ADULT;    // BR 10-20 rpm, HR 40-100 bpm
  static const VitalProfile TODDLER;  // BR 16-45 rpm, HR 60-160 bpm

  // ── Config structs (passed once to begin()) ───────────────────────────
  // VitalConfig: person — vital sign thresholds + debounce timings
  struct VitalConfig {
    VitalProfile profile;
    uint32_t     zeroDebounceMs   = 20000;
    uint32_t     threshDebounceMs = 15000;
  };

  // LightConfig: environment — independent of person config
  struct LightConfig {
    int     threshold = 10;              // lux boundary between dark and light
    uint8_t trackMode = LIGHT_TRACK_MODE;
  };

  // ── Events ───────────────────────────────────────────────────────────
  enum Event {
    EVT_PRESENCE_ON, EVT_PRESENCE_OFF,
    EVT_NO_BREATHING, EVT_LOW_BREATHING, EVT_HIGH_BREATHING, EVT_IRREGULAR_BREATHING,
    EVT_NO_HEART_RATE, EVT_LOW_HEART_RATE, EVT_HIGH_HEART_RATE,
    EVT_BECAME_LIGHT, EVT_BECAME_DARK,
  };
  // Callback receives kit reference (query any state), event type, and the
  // reading at fire time: br (rpm) / hr (bpm) / lux; 0 for presence events.
  typedef void (*EventCallback)(mmWaveKit& kit, Event e, int value);

  // ── Alert state snapshot ──────────────────────────────────────────────
  struct AlertState {
    bool noBreathing, lowBreathing, highBreathing, irregularBreathing;
    bool noHeartRate, lowHeartRate, highHeartRate;
  };

  // ── Lifecycle ─────────────────────────────────────────────────────────
  bool begin(const VitalConfig& vc, const LightConfig& lc);
  bool begin(HardwareSerial& serial, const VitalConfig& vc, const LightConfig& lc);
  void update();  // call every loop() — drains sensor, evaluates alerts every 1 s

  // ── Readings (all int — no fractions) ────────────────────────────────
  int  getBreathingRate() const { return (int)_br; }
  int  getHeartRate()     const { return (int)_hr; }
  int  getLux()           const { return (int)_lux; }
  bool isPresent()        const { return _presence; }
  bool isLight()          const { return (int)_lux >= _lightCfg.threshold; }
  bool isDark()           const { return (int)_lux <  _lightCfg.threshold; }
  int  getThreshold()     const { return _lightCfg.threshold; }
  bool isTrackingActive() const;
  const AlertState& getAlerts() const { return _alerts; }

  // ── Callbacks — general + per-event (Button2 pattern) ─────────────────
  // General: fires for every event before the specific handler
  void onEvent(EventCallback cb)              { _onEvent = cb; }

  // Presence
  void onPresenceOn(EventCallback cb)         { _onPresenceOn = cb; }
  void onPresenceOff(EventCallback cb)        { _onPresenceOff = cb; }

  // Breathing
  void onNoBreathing(EventCallback cb)        { _onNoBreathing = cb; }
  void onBreathingLow(EventCallback cb)       { _onBreathingLow = cb; }
  void onBreathingHigh(EventCallback cb)      { _onBreathingHigh = cb; }
  void onBreathingIrregular(EventCallback cb) { _onBreathingIrregular = cb; }

  // Heart rate
  void onNoHeartRate(EventCallback cb)        { _onNoHeartRate = cb; }
  void onHeartRateLow(EventCallback cb)       { _onHeartRateLow = cb; }
  void onHeartRateHigh(EventCallback cb)      { _onHeartRateHigh = cb; }

  // Light — specific + combined
  void onBecameLight(EventCallback cb)           { _onBecameLight = cb; }
  void onBecameDark(EventCallback cb)            { _onBecameDark = cb; }
  void onLightSituationChanged(EventCallback cb) { _onLightSituationChanged = cb; }

  // ── LED ───────────────────────────────────────────────────────────────
  void setLedColor(uint8_t r, uint8_t g, uint8_t b);
  void setLedOff();

private:
  float _br = 0.0f, _hr = 0.0f, _lux = 0.0f;
  bool  _presence = false, _prevPresence = false;

  VitalConfig _vitalCfg;
  LightConfig _lightCfg;

  AlertState    _alerts = {}, _prevAlerts = {};
  unsigned long _zeroBreathSince = 0, _lowBreathSince  = 0, _highBreathSince = 0;
  unsigned long _zeroHrSince     = 0, _lowHrSince      = 0, _highHrSince     = 0;
  unsigned long _lastEval        = 0;
  bool          _prevLight       = false;

  static const uint8_t IRREG_WINDOW = 60;
  float   _brWindow[IRREG_WINDOW] = {};
  uint8_t _brWindowIdx = 0, _brWindowFill = 0;

  // One slot per event + general — one callback each, like Button2
  EventCallback _onEvent              = nullptr;
  EventCallback _onPresenceOn         = nullptr;
  EventCallback _onPresenceOff        = nullptr;
  EventCallback _onNoBreathing        = nullptr;
  EventCallback _onBreathingLow       = nullptr;
  EventCallback _onBreathingHigh      = nullptr;
  EventCallback _onBreathingIrregular = nullptr;
  EventCallback _onNoHeartRate        = nullptr;
  EventCallback _onHeartRateLow       = nullptr;
  EventCallback _onHeartRateHigh      = nullptr;
  EventCallback _onBecameLight           = nullptr;
  EventCallback _onBecameDark            = nullptr;
  EventCallback _onLightSituationChanged = nullptr;

  void  _evalVitals(unsigned long now);
  void  _evalLight();
  void  _debounce(bool cond, unsigned long now, unsigned long& since,
                  uint32_t ms, bool& flag);
  float _brStddev();
  void  _fire(Event e, int value = 0);
};
