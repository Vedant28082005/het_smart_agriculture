// Pure helpers for the ESP32 HTTP API: tiny JSON field readers, API-key check, response builders.
// No Arduino calls, so they are unit-tested on a PC. The request bodies are small flat objects,
// so a full JSON library is not needed.
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "payload.h"

// Finds `"key"` followed by ':' and returns a pointer to the value start, or nullptr.
inline const char *jsonFindValue(const char *body, const char *key) {
  char pat[40];
  if (snprintf(pat, sizeof pat, "\"%s\"", key) >= (int)sizeof pat) return nullptr;
  const char *p = strstr(body, pat);
  if (!p) return nullptr;
  p += strlen(pat);
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  if (*p != ':') return nullptr;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  return p;
}

// Integer value (a JSON number without fraction/exponent). False if missing or not an integer.
inline bool jsonGetInt(const char *body, const char *key, long *out) {
  const char *p = jsonFindValue(body, key);
  if (!p) return false;
  char *end;
  long v = strtol(p, &end, 10);
  if (end == p) return false;
  if (*end == '.' || *end == 'e' || *end == 'E') return false;
  *out = v;
  return true;
}

// Simple string value without escapes. False if missing, not a string, or too long.
inline bool jsonGetStr(const char *body, const char *key, char *out, size_t n) {
  const char *p = jsonFindValue(body, key);
  if (!p || *p != '"') return false;
  p++;
  const char *q = strchr(p, '"');
  if (!q || (size_t)(q - p) >= n) return false;
  memcpy(out, p, q - p);
  out[q - p] = 0;
  return true;
}

// Constant-time compare so response timing does not leak the key. Empty keys never match.
inline bool apiKeyOk(const char *given, const char *expected) {
  if (!given || !expected || !*expected) return false;
  size_t a = strlen(given), b = strlen(expected);
  unsigned diff = (unsigned)(a ^ b);
  for (size_t i = 0; i < b; i++) diff |= (unsigned)((unsigned char)given[i < a ? i : 0] ^ (unsigned char)expected[i]);
  return diff == 0;
}

inline int buildErrorJson(char *buf, size_t n, const char *error, const char *reason = nullptr) {
  int len = reason ? snprintf(buf, n, "{\"error\":\"%s\",\"reason\":\"%s\"}", error, reason)
                   : snprintf(buf, n, "{\"error\":\"%s\"}", error);
  return (len < 0 || (size_t)len >= n) ? -1 : len;
}

// Same fields as the MQTT sensors message plus thresholds and uptime.
inline int buildStatusJson(char *buf, size_t n, const SensorSnapshot &s, const Thresholds &t, uint32_t uptimeS) {
  int len = buildSensorsJson(buf, n, s);
  if (len < 0) return -1;
  int extra = snprintf(buf + len - 1, n - (len - 1),
      ",\"thresholds\":{\"on\":%d,\"off\":%d,\"max_run_s\":%lu,\"cooldown_s\":%lu},\"uptime_s\":%lu}",
      t.on, t.off, (unsigned long)t.maxRunS, (unsigned long)t.cooldownS, (unsigned long)uptimeS);
  return (extra < 0 || (size_t)extra >= n - (len - 1)) ? -1 : len - 1 + extra;
}

// Pump command result -> HTTP status. 200 for Ok, 409 for everything refused by a rule.
struct CmdHttp { int status; const char *error; const char *reason; };
inline CmdHttp cmdResultHttp(CmdResult r) {
  switch (r) {
    case CmdResult::Ok:          return {200, nullptr, nullptr};
    case CmdResult::NotManual:   return {409, "Pump can only be commanded in manual mode", nullptr};
    case CmdResult::TankEmpty:   return {409, "Tank empty", "LOCKOUT_TANK_EMPTY"};
    case CmdResult::CoolingDown: return {409, "Cooling down", "LOCKOUT_MAX_RUNTIME"};
  }
  return {500, "internal", nullptr};
}

// Parse the /thresholds body. All four fields are required and must be integers in int/uint range.
inline bool parseThresholds(const char *body, Thresholds *out) {
  long on, off, run, cool;
  if (!jsonGetInt(body, "on", &on) || !jsonGetInt(body, "off", &off) ||
      !jsonGetInt(body, "max_run_s", &run) || !jsonGetInt(body, "cooldown_s", &cool)) return false;
  if (run < 0 || cool < 0) return false;
  Thresholds t;
  t.on = (int)on; t.off = (int)off; t.maxRunS = (uint32_t)run; t.cooldownS = (uint32_t)cool;
  *out = t;
  return true;  // range rules are checked by thresholdsValid()
}
