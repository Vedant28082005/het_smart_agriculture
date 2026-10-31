// JSON payloads for MQTT. Plain snprintf, no libraries, so it is testable on a PC.
// Field names match what backend/app/ingest.py and the dashboard expect.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include "controller.h"

struct SensorSnapshot {
  uint32_t ts;      // epoch seconds if NTP synced, else 0 (backend then uses arrival time)
  float soil;
  int soilRaw;
  float temp;
  float hum;
  bool tankOk;
  bool pump;
  Mode mode;
  Reason reason;
  bool sensorOk;
  int rssi;
};

// Returns the length written, or -1 if it did not fit.
inline int buildSensorsJson(char *buf, size_t n, const SensorSnapshot &s) {
  int len = snprintf(buf, n,
      "{\"ts\":%lu,\"soil\":%.1f,\"soil_raw\":%d,\"temp\":%.1f,\"hum\":%.1f,"
      "\"tank_ok\":%s,\"pump\":%s,\"mode\":\"%s\",\"reason\":\"%s\",\"sensor_ok\":%s,\"rssi\":%d}",
      (unsigned long)s.ts, s.soil, s.soilRaw, s.temp, s.hum,
      s.tankOk ? "true" : "false", s.pump ? "true" : "false",
      s.mode == Mode::Auto ? "auto" : "manual", reasonStr(s.reason),
      s.sensorOk ? "true" : "false", s.rssi);
  return (len < 0 || (size_t)len >= n) ? -1 : len;
}

inline const char *eventTypeStr(EventKind k) {
  return k == EventKind::Alert ? "alert" : "pump";
}

inline const char *eventMessage(EventKind k, Reason r) {
  if (k == EventKind::PumpOn) return "Pump ON";
  if (k == EventKind::PumpOff) return "Pump OFF";
  if (r == Reason::LockoutTankEmpty) return "Tank empty";
  if (r == Reason::LockoutMaxRuntime) return "Max run time reached";
  if (r == Reason::LockoutSensorFail) return "Soil sensor failure";
  return "Alert";
}

inline int buildEventJson(char *buf, size_t n, uint32_t ts, const Event &e) {
  int len = snprintf(buf, n, "{\"ts\":%lu,\"type\":\"%s\",\"message\":\"%s\",\"reason\":\"%s\"}",
                     (unsigned long)ts, eventTypeStr(e.kind), eventMessage(e.kind, e.reason), reasonStr(e.reason));
  return (len < 0 || (size_t)len >= n) ? -1 : len;
}
