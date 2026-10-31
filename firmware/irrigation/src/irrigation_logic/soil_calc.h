// Pure C++ (no Arduino calls) so it can be unit-tested on a PC.
#pragma once

// Linear map raw -> 0..100 % between dry and wet raw values, clamped.
// Works whether wet < dry (capacitive sensors) or the reverse.
inline float soilPercent(int raw, int dryRaw, int wetRaw) {
  if (dryRaw == wetRaw) return 0.0f;
  float pct = 100.0f * (float)(raw - dryRaw) / (float)(wetRaw - dryRaw);
  if (pct < 0.0f) return 0.0f;
  if (pct > 100.0f) return 100.0f;
  return pct;
}

inline bool soilRawValid(int raw, int minValid, int maxValid) {
  return raw >= minValid && raw <= maxValid;
}
