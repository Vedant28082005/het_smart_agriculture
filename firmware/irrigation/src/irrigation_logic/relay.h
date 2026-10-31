// Pin level for a relay given the desired pump state and the module's polarity.
#pragma once

inline bool relayLevelHigh(bool pumpOn, bool activeLow) {
  return activeLow ? !pumpOn : pumpOn;
}
