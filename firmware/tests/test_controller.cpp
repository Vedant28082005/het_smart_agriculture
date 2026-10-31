// Host-side unit tests for the control logic. No framework: plain asserts.
// Build and run: see firmware/tests/README.md
#include <cstdio>
#include <cstring>
#include "../irrigation/src/irrigation_logic/controller.h"
#include "../irrigation/src/irrigation_logic/relay.h"
#include "../irrigation/src/irrigation_logic/soil_calc.h"

static int failed = 0, total = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    total++;                                                          \
    if (!(cond)) { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static Inputs in(uint32_t ms, float soil, bool sensorOk = true, bool tankOk = true) {
  return Inputs{ms, soil, sensorOk, tankOk};
}

static void boot_is_off() {
  Controller c;
  CHECK(!c.pumpOn());
  CHECK(c.mode() == Mode::Auto);
  CHECK(c.reason() == Reason::Boot);
}

static void hysteresis() {
  Controller c;  // on 30, off 60
  CHECK(c.update(in(0, 45)).kind == EventKind::None);
  CHECK(!c.pumpOn());
  Event e = c.update(in(1000, 29.9f));
  CHECK(e.kind == EventKind::PumpOn && e.reason == Reason::AutoLowMoisture);
  CHECK(c.pumpOn());
  // Inside the band the pump keeps running, and does not chatter.
  CHECK(c.update(in(2000, 45)).kind == EventKind::None);
  CHECK(c.pumpOn());
  CHECK(c.update(in(3000, 60)).kind == EventKind::None);  // exactly OFF threshold: still on
  CHECK(c.pumpOn());
  e = c.update(in(4000, 60.1f));
  CHECK(e.kind == EventKind::PumpOff && e.reason == Reason::AutoTargetReached);
  CHECK(!c.pumpOn());
  // Back inside the band after stopping: stays off.
  CHECK(c.update(in(5000, 45)).kind == EventKind::None);
  CHECK(!c.pumpOn());
  CHECK(c.update(in(6000, 30)).kind == EventKind::None);  // exactly ON threshold: no start
  CHECK(!c.pumpOn());
}

static void tank_empty_blocks_and_stops() {
  Controller c;
  c.update(in(0, 10));
  CHECK(c.pumpOn());
  Event e = c.update(in(1000, 10, true, false));
  CHECK(!c.pumpOn());
  CHECK(e.kind == EventKind::Alert && e.reason == Reason::LockoutTankEmpty);
  // Alert only once while the tank stays empty.
  CHECK(c.update(in(2000, 10, true, false)).kind == EventKind::None);
  CHECK(!c.pumpOn());
  // Refilled: soil still dry, so auto resumes.
  Event r = c.update(in(3000, 10));
  CHECK(r.kind == EventKind::PumpOn);
  CHECK(c.pumpOn());
}

static void sensor_failure_locks_auto() {
  Controller c;
  Event e = c.update(in(0, 10, false));
  CHECK(!c.pumpOn());
  CHECK(e.kind == EventKind::Alert && e.reason == Reason::LockoutSensorFail);
  // Running pump is stopped when the sensor dies.
  Controller d;
  d.update(in(0, 10));
  CHECK(d.pumpOn());
  d.update(in(1000, 10, false));
  CHECK(!d.pumpOn());
  CHECK(d.reason() == Reason::LockoutSensorFail);
}

static void max_runtime_and_cooldown() {
  Thresholds t;
  t.maxRunS = 60;
  t.cooldownS = 300;
  Controller c(t);
  c.update(in(0, 10));
  CHECK(c.pumpOn());
  CHECK(c.update(in(59000, 10)).kind == EventKind::None);
  CHECK(c.pumpOn());
  Event e = c.update(in(60000, 10));
  CHECK(!c.pumpOn());
  CHECK(e.kind == EventKind::Alert && e.reason == Reason::LockoutMaxRuntime);
  // Soil still dry, but cooldown blocks restarting.
  CHECK(c.update(in(120000, 10)).kind == EventKind::None);
  CHECK(!c.pumpOn());
  CHECK(c.update(in(359000, 10)).kind == EventKind::None);
  CHECK(!c.pumpOn());
  CHECK(c.cooldownLeftS(359000) == 1);
  // Cooldown over (300 s after the stop at 60 s).
  Event r = c.update(in(360000, 10));
  CHECK(r.kind == EventKind::PumpOn && c.pumpOn());
  CHECK(c.cooldownLeftS(360000) == 0);
}

static void millis_rollover() {
  Controller c;
  uint32_t start = 0xFFFFFFFFu - 10000;  // 10 s before millis() wraps
  c.update(in(start, 10));
  CHECK(c.pumpOn());
  CHECK(c.update(in(start + 50000u, 10)).kind == EventKind::None);  // 50 s run, wrapped
  CHECK(c.pumpOn());
  Event e = c.update(in(start + 60000u, 10));  // 60 s run
  CHECK(e.kind == EventKind::Alert && e.reason == Reason::LockoutMaxRuntime);
  CHECK(!c.pumpOn());
  CHECK(c.cooldownLeftS(start + 61000u) == 299);
}

static void manual_mode() {
  Controller c;
  // Not allowed in auto.
  CHECK(c.commandPump(true, in(0, 50)) == CmdResult::NotManual);
  CHECK(!c.pumpOn());

  c.setMode(Mode::Manual, 0);
  CHECK(c.reason() == Reason::ManualOff);
  // Auto logic is inactive: dry soil does not start the pump.
  c.update(in(1000, 5));
  CHECK(!c.pumpOn());

  Event ev;
  CHECK(c.commandPump(true, in(2000, 5), &ev) == CmdResult::Ok);
  CHECK(c.pumpOn() && ev.kind == EventKind::PumpOn && c.reason() == Reason::ManualOn);
  // Manual run is not stopped by the auto OFF threshold...
  c.update(in(3000, 99));
  CHECK(c.pumpOn());
  // ...but is stopped by the max-runtime safety.
  c.update(in(62000, 99));
  CHECK(!c.pumpOn());
  CHECK(c.reason() == Reason::LockoutMaxRuntime);
  CHECK(c.commandPump(true, in(63000, 99)) == CmdResult::CoolingDown);
  CHECK(!c.pumpOn());
}

static void manual_respects_tank_and_off_works() {
  Controller c;
  c.setMode(Mode::Manual, 0);
  CHECK(c.commandPump(true, in(0, 50, true, false)) == CmdResult::TankEmpty);
  CHECK(!c.pumpOn());
  CHECK(c.commandPump(true, in(0, 50)) == CmdResult::Ok);
  Event ev;
  CHECK(c.commandPump(false, in(1000, 50), &ev) == CmdResult::Ok);
  CHECK(!c.pumpOn() && ev.kind == EventKind::PumpOff && c.reason() == Reason::ManualOff);
  // Turning off an already-off pump is fine and silent.
  Event ev2;
  CHECK(c.commandPump(false, in(2000, 50), &ev2) == CmdResult::Ok);
  CHECK(ev2.kind == EventKind::None);
}

static void manual_tank_empty_stops_running_pump() {
  Controller c;
  c.setMode(Mode::Manual, 0);
  c.commandPump(true, in(0, 50));
  CHECK(c.pumpOn());
  c.update(in(1000, 50, true, false));
  CHECK(!c.pumpOn());
  CHECK(c.reason() == Reason::LockoutTankEmpty);
}

static void manual_ignores_sensor_failure() {
  Controller c;
  c.setMode(Mode::Manual, 0);
  CHECK(c.commandPump(true, in(0, 0, false)) == CmdResult::Ok);
  c.update(in(1000, 0, false));
  CHECK(c.pumpOn());
}

static void mode_switch_stops_pump() {
  Controller c;
  c.update(in(0, 10));
  CHECK(c.pumpOn());
  c.setMode(Mode::Manual, 1000);
  CHECK(!c.pumpOn());
  c.setMode(Mode::Auto, 2000);
  CHECK(!c.pumpOn());
  // Auto resumes on the next update when soil is dry.
  CHECK(c.update(in(3000, 10)).kind == EventKind::PumpOn);
}

static void threshold_validation() {
  Controller c;
  Thresholds t;
  t.on = 60; t.off = 30;
  CHECK(!c.setThresholds(t));
  t.on = 30; t.off = 30;
  CHECK(!c.setThresholds(t));
  t.on = -1; t.off = 50;
  CHECK(!c.setThresholds(t));
  t.on = 20; t.off = 101;
  CHECK(!c.setThresholds(t));
  t.on = 20; t.off = 50; t.maxRunS = 0;
  CHECK(!c.setThresholds(t));
  t.maxRunS = 3601;
  CHECK(!c.setThresholds(t));
  t.maxRunS = 30; t.cooldownS = 86401;
  CHECK(!c.setThresholds(t));
  t.cooldownS = 10;
  CHECK(c.setThresholds(t));
  CHECK(c.thresholds().on == 20 && c.thresholds().maxRunS == 30);
  // Rejected values did not change the active ones.
  Thresholds bad; bad.on = 90; bad.off = 10;
  c.setThresholds(bad);
  CHECK(c.thresholds().on == 20);
}

static void reason_strings_match_dashboard() {
  CHECK(strcmp(reasonStr(Reason::AutoLowMoisture), "AUTO_LOW_MOISTURE") == 0);
  CHECK(strcmp(reasonStr(Reason::AutoTargetReached), "AUTO_TARGET_REACHED") == 0);
  CHECK(strcmp(reasonStr(Reason::ManualOn), "MANUAL_ON") == 0);
  CHECK(strcmp(reasonStr(Reason::ManualOff), "MANUAL_OFF") == 0);
  CHECK(strcmp(reasonStr(Reason::LockoutTankEmpty), "LOCKOUT_TANK_EMPTY") == 0);
  CHECK(strcmp(reasonStr(Reason::LockoutMaxRuntime), "LOCKOUT_MAX_RUNTIME") == 0);
  CHECK(strcmp(reasonStr(Reason::LockoutSensorFail), "LOCKOUT_SENSOR_FAIL") == 0);
}

static void relay_polarity() {
  CHECK(relayLevelHigh(false, true) == true);   // active-low: OFF = HIGH
  CHECK(relayLevelHigh(true, true) == false);   // active-low: ON = LOW
  CHECK(relayLevelHigh(false, false) == false);
  CHECK(relayLevelHigh(true, false) == true);
}

static void soil_calc() {
  CHECK(soilPercent(3200, 3200, 1400) == 0.0f);
  CHECK(soilPercent(1400, 3200, 1400) == 100.0f);
  CHECK(soilPercent(9999, 3200, 1400) == 0.0f);   // clamped
  CHECK(soilPercent(0, 3200, 1400) == 100.0f);    // clamped
}

int main() {
  boot_is_off();
  hysteresis();
  tank_empty_blocks_and_stops();
  sensor_failure_locks_auto();
  max_runtime_and_cooldown();
  millis_rollover();
  manual_mode();
  manual_respects_tank_and_off_works();
  manual_tank_empty_stops_running_pump();
  manual_ignores_sensor_failure();
  mode_switch_stops_pump();
  threshold_validation();
  reason_strings_match_dashboard();
  relay_polarity();
  soil_calc();
  printf("%d checks, %d failed\n", total, failed);
  return failed ? 1 : 0;
}
