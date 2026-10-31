// Pump control logic. No Arduino or hardware calls: time and sensor values are passed in,
// so it can be unit-tested on a PC (firmware/tests).
#pragma once
#include <stdint.h>

enum class Mode : uint8_t { Auto, Manual };

enum class Reason : uint8_t {
  Boot,
  AutoLowMoisture,
  AutoTargetReached,
  ManualOn,
  ManualOff,
  LockoutTankEmpty,
  LockoutMaxRuntime,
  LockoutSensorFail,
};

// Same strings the backend and dashboard use.
inline const char *reasonStr(Reason r) {
  switch (r) {
    case Reason::Boot:              return "BOOT";
    case Reason::AutoLowMoisture:   return "AUTO_LOW_MOISTURE";
    case Reason::AutoTargetReached: return "AUTO_TARGET_REACHED";
    case Reason::ManualOn:          return "MANUAL_ON";
    case Reason::ManualOff:         return "MANUAL_OFF";
    case Reason::LockoutTankEmpty:  return "LOCKOUT_TANK_EMPTY";
    case Reason::LockoutMaxRuntime: return "LOCKOUT_MAX_RUNTIME";
    case Reason::LockoutSensorFail: return "LOCKOUT_SENSOR_FAIL";
  }
  return "UNKNOWN";
}

struct Thresholds {
  int on = 30;             // start pumping below this soil %
  int off = 60;            // stop pumping above this soil %
  uint32_t maxRunS = 60;   // longest continuous run
  uint32_t cooldownS = 300;  // rest after hitting maxRunS
};

inline bool thresholdsValid(const Thresholds &t) {
  return t.on >= 0 && t.off <= 100 && t.on < t.off &&
         t.maxRunS >= 1 && t.maxRunS <= 3600 && t.cooldownS <= 86400;
}

struct Inputs {
  uint32_t nowMs;  // millis()
  float soil;      // percent, only meaningful when sensorOk
  bool sensorOk;
  bool tankOk;
};

// What happened during an update or command, for the event log / MQTT.
enum class EventKind : uint8_t { None, PumpOn, PumpOff, Alert };
struct Event {
  EventKind kind = EventKind::None;
  Reason reason = Reason::Boot;
};

enum class CmdResult : uint8_t { Ok, NotManual, TankEmpty, CoolingDown };

class Controller {
 public:
  explicit Controller(Thresholds t = Thresholds()) : th_(t) {}

  bool pumpOn() const { return pump_; }
  Mode mode() const { return mode_; }
  Reason reason() const { return reason_; }
  const Thresholds &thresholds() const { return th_; }

  bool setThresholds(const Thresholds &t) {
    if (!thresholdsValid(t)) return false;
    th_ = t;
    return true;
  }

  // Switching mode always turns the pump off; auto then decides on the next update().
  void setMode(Mode m, uint32_t nowMs) {
    if (m == mode_) return;
    mode_ = m;
    if (pump_) stop(nowMs);
    reason_ = (m == Mode::Manual) ? Reason::ManualOff : Reason::AutoTargetReached;
  }

  // Manual pump command. Safety lockouts are never bypassed.
  CmdResult commandPump(bool on, const Inputs &in, Event *ev = nullptr) {
    if (mode_ != Mode::Manual) return CmdResult::NotManual;
    if (!on) {
      if (pump_) { stop(in.nowMs); reason_ = Reason::ManualOff; setEv(ev, EventKind::PumpOff); }
      return CmdResult::Ok;
    }
    if (!in.tankOk) return CmdResult::TankEmpty;
    if (cooling(in.nowMs)) return CmdResult::CoolingDown;
    if (!pump_) { start(in.nowMs); setEv(ev, EventKind::PumpOn); }
    reason_ = Reason::ManualOn;
    return CmdResult::Ok;
  }

  // Call often (every loop). Returns at most one event per call.
  Event update(const Inputs &in) {
    Event ev;

    // 1. Hard safety stops, in priority order.
    if (!in.tankOk) {
      return lockout(in, Reason::LockoutTankEmpty, ev);
    }
    if (pump_ && (uint32_t)(in.nowMs - runStartMs_) >= th_.maxRunS * 1000UL) {
      stop(in.nowMs);
      coolStartMs_ = in.nowMs;
      cooling_ = true;
      reason_ = Reason::LockoutMaxRuntime;
      ev.kind = EventKind::Alert;
      ev.reason = reason_;
      return ev;
    }
    if (mode_ == Mode::Auto && !in.sensorOk) {
      return lockout(in, Reason::LockoutSensorFail, ev);
    }

    // 2. Cooldown blocks every start.
    if (cooling(in.nowMs)) {
      if (pump_) stop(in.nowMs);
      return ev;
    }

    // 3. Automatic hysteresis.
    if (mode_ == Mode::Auto) {
      if (!pump_ && in.soil < th_.on) {
        start(in.nowMs);
        reason_ = Reason::AutoLowMoisture;
        ev.kind = EventKind::PumpOn;
        ev.reason = reason_;
      } else if (pump_ && in.soil > th_.off) {
        stop(in.nowMs);
        reason_ = Reason::AutoTargetReached;
        ev.kind = EventKind::PumpOff;
        ev.reason = reason_;
      } else if (!pump_ && isLockout(reason_)) {
        // Lockout cause is gone and soil is between thresholds: back to normal idle.
        reason_ = Reason::AutoTargetReached;
      }
    } else if (!pump_ && isLockout(reason_)) {
      reason_ = Reason::ManualOff;
    }
    return ev;
  }

  uint32_t cooldownLeftS(uint32_t nowMs) const {
    if (!cooling(nowMs)) return 0;
    uint32_t elapsed = nowMs - coolStartMs_;
    return (th_.cooldownS * 1000UL - elapsed + 999) / 1000;
  }

 private:
  Thresholds th_;
  Mode mode_ = Mode::Auto;
  Reason reason_ = Reason::Boot;
  bool pump_ = false;
  bool cooling_ = false;
  uint32_t runStartMs_ = 0;
  uint32_t coolStartMs_ = 0;

  static bool isLockout(Reason r) {
    return r == Reason::LockoutTankEmpty || r == Reason::LockoutMaxRuntime || r == Reason::LockoutSensorFail;
  }
  static void setEv(Event *ev, EventKind k) { if (ev) ev->kind = k; }

  bool cooling(uint32_t nowMs) const {
    return cooling_ && (uint32_t)(nowMs - coolStartMs_) < th_.cooldownS * 1000UL;
  }
  void start(uint32_t nowMs) { pump_ = true; runStartMs_ = nowMs; }
  void stop(uint32_t) { pump_ = false; }

  // Pump off with a lockout reason; raise an alert only when entering the lockout.
  Event lockout(const Inputs &in, Reason r, Event ev) {
    bool entering = (reason_ != r);
    if (pump_) stop(in.nowMs);
    reason_ = r;
    if (entering) { ev.kind = EventKind::Alert; ev.reason = r; }
    return ev;
  }
};
