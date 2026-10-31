// Reconnect pacing: wait 1 s, 2 s, 4 s ... up to a cap, restart from the beginning after a success.
// Time is passed in (millis), rollover-safe.
#pragma once
#include <stdint.h>

class Backoff {
 public:
  Backoff(uint32_t minMs = 1000, uint32_t maxMs = 60000) : min_(minMs), max_(maxMs), delay_(minMs) {}

  // True when an attempt may be made now.
  bool due(uint32_t nowMs) const { return !waiting_ || (uint32_t)(nowMs - lastMs_) >= delay_; }

  // Call right after an attempt that failed.
  void failed(uint32_t nowMs) {
    if (waiting_) delay_ = (delay_ * 2 > max_) ? max_ : delay_ * 2;
    waiting_ = true;
    lastMs_ = nowMs;
  }

  void succeeded() { waiting_ = false; delay_ = min_; }

  uint32_t currentDelayMs() const { return delay_; }

 private:
  uint32_t min_, max_, delay_;
  uint32_t lastMs_ = 0;
  bool waiting_ = false;
};
