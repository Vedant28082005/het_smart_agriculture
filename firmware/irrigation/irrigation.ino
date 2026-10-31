// Smart irrigation, Phase 2: sensors, calibration and pump control (serial-driven).
// Libraries (Arduino IDE Library Manager): "DHT sensor library for ESPx" (DHTesp).
// Board: "ESP32 Dev Module".
#include <Preferences.h>
#include <DHTesp.h>
#include "config.h"
#include "src/irrigation_logic/soil_calc.h"
#include "src/irrigation_logic/controller.h"
#include "src/irrigation_logic/relay.h"

Preferences prefs;
DHTesp dht;

int soilDry = SOIL_DEFAULT_DRY;
int soilWet = SOIL_DEFAULT_WET;

int soilRaw = 0;
float soilPct = 0;
bool soilOk = false;

float temp = NAN, hum = NAN;
bool dhtOk = false;
const char *dhtStatus = "";
unsigned long lastDhtGoodMs = 0;

bool tankOk = false;

Controller ctrl;

unsigned long lastSensorMs = 0;
String cmdBuf;

int readSoilRawAvg() {
  long sum = 0;
  for (int i = 0; i < SOIL_SAMPLES; i++) {
    sum += analogRead(PIN_SOIL_AOUT);
    delayMicroseconds(200);
  }
  return (int)(sum / SOIL_SAMPLES);
}

void saveCal() {
  prefs.putInt("dry", soilDry);
  prefs.putInt("wet", soilWet);
}

void setRelay(bool on) {
  digitalWrite(PIN_RELAY, relayLevelHigh(on, RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void saveThresholds() {
  const Thresholds &t = ctrl.thresholds();
  prefs.putInt("th_on", t.on);
  prefs.putInt("th_off", t.off);
  prefs.putUInt("th_run", t.maxRunS);
  prefs.putUInt("th_cool", t.cooldownS);
}

void loadThresholds() {
  Thresholds t;
  t.on = prefs.getInt("th_on", t.on);
  t.off = prefs.getInt("th_off", t.off);
  t.maxRunS = prefs.getUInt("th_run", t.maxRunS);
  t.cooldownS = prefs.getUInt("th_cool", t.cooldownS);
  ctrl.setThresholds(t);  // invalid stored values are ignored, defaults stay
}

Inputs currentInputs() {
  return Inputs{millis(), soilPct, soilOk, tankOk};
}

void logEvent(const Event &e) {
  if (e.kind == EventKind::None) return;
  const char *what = e.kind == EventKind::PumpOn ? "PUMP ON" : e.kind == EventKind::PumpOff ? "PUMP OFF" : "ALERT";
  Serial.printf("EVENT %s reason=%s\n", what, reasonStr(e.reason));
}

void handleCommand(String c) {
  c.trim();
  c.toLowerCase();
  if (c == "cal dry") {
    soilDry = readSoilRawAvg();
    saveCal();
    Serial.printf("OK dry=%d (sensor in AIR)\n", soilDry);
  } else if (c == "cal wet") {
    soilWet = readSoilRawAvg();
    saveCal();
    Serial.printf("OK wet=%d (sensor in WATER up to the line)\n", soilWet);
  } else if (c == "cal show") {
    Serial.printf("dry=%d wet=%d\n", soilDry, soilWet);
  } else if (c == "cal reset") {
    soilDry = SOIL_DEFAULT_DRY;
    soilWet = SOIL_DEFAULT_WET;
    saveCal();
    Serial.println("OK calibration reset to defaults");
  } else if (c == "mode auto" || c == "mode manual") {
    ctrl.setMode(c == "mode auto" ? Mode::Auto : Mode::Manual, millis());
    setRelay(ctrl.pumpOn());
    Serial.printf("OK mode=%s\n", c == "mode auto" ? "auto" : "manual");
  } else if (c == "pump on" || c == "pump off") {
    Event e;
    CmdResult r = ctrl.commandPump(c == "pump on", currentInputs(), &e);
    setRelay(ctrl.pumpOn());
    logEvent(e);
    if (r == CmdResult::Ok) Serial.println("OK");
    else if (r == CmdResult::NotManual) Serial.println("REFUSED: switch to 'mode manual' first");
    else if (r == CmdResult::TankEmpty) Serial.println("REFUSED: tank empty");
    else Serial.println("REFUSED: cooling down after max run time");
  } else if (c == "th show") {
    const Thresholds &t = ctrl.thresholds();
    Serial.printf("on<%d off>%d max_run=%us cooldown=%us\n", t.on, t.off, (unsigned)t.maxRunS, (unsigned)t.cooldownS);
  } else if (c.startsWith("th ")) {
    // th <on%> <off%> <max_run_s> <cooldown_s>
    int on, off;
    unsigned run, cool;
    Thresholds t;
    if (sscanf(c.c_str(), "th %d %d %u %u", &on, &off, &run, &cool) == 4) {
      t.on = on; t.off = off; t.maxRunS = run; t.cooldownS = cool;
      if (ctrl.setThresholds(t)) {
        saveThresholds();
        Serial.println("OK thresholds saved");
      } else {
        Serial.println("REFUSED: need 0<=on<off<=100, 1<=run<=3600, 0<=cool<=86400");
      }
    } else {
      Serial.println("usage: th <on%> <off%> <max_run_s> <cooldown_s>");
    }
  } else if (c == "help") {
    Serial.println("Commands: cal dry|wet|show|reset | mode auto|manual | pump on|off | th <on> <off> <run_s> <cool_s> | th show | help");
  } else if (c.length()) {
    Serial.println("Unknown command, type: help");
  }
}

void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      handleCommand(cmdBuf);
      cmdBuf = "";
    } else if (cmdBuf.length() < 32) {
      cmdBuf += ch;
    }
  }
}

void readSensors() {
  soilRaw = readSoilRawAvg();
  soilOk = soilRawValid(soilRaw, SOIL_RAW_MIN_VALID, SOIL_RAW_MAX_VALID);
  soilPct = soilPercent(soilRaw, soilDry, soilWet);

  TempAndHumidity th = dht.getTempAndHumidity();
  if (dht.getStatus() == DHTesp::ERROR_NONE && !isnan(th.temperature) && !isnan(th.humidity)) {
    temp = th.temperature;  // keep last good value on failure
    hum = th.humidity;
    lastDhtGoodMs = millis();
  }
  // unsigned subtraction is safe across millis() rollover
  dhtStatus = dht.getStatusString();
  dhtOk = (lastDhtGoodMs != 0) && (millis() - lastDhtGoodMs < DHT_FAIL_TIMEOUT_MS);

#if ENABLE_FLOAT_SWITCH
  tankOk = (digitalRead(PIN_FLOAT) == FLOAT_WATER_LEVEL);
#else
  tankOk = true;  // no float switch: tank level is not monitored
#endif
}

void setup() {
  // Relay OFF first, before anything else.
  pinMode(PIN_RELAY, OUTPUT);
  setRelay(false);

  Serial.begin(SERIAL_BAUD);
#if ENABLE_FLOAT_SWITCH
  pinMode(PIN_FLOAT, INPUT_PULLUP);
#endif
  pinMode(PIN_STATUS_LED, OUTPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_SOIL_AOUT, ADC_11db);

  dht.setup(PIN_DHT, USE_DHT22 ? DHTesp::DHT22 : DHTesp::DHT11);

  prefs.begin("irrig", false);
  soilDry = prefs.getInt("dry", SOIL_DEFAULT_DRY);
  soilWet = prefs.getInt("wet", SOIL_DEFAULT_WET);

  loadThresholds();

  Serial.println("\n== Smart Irrigation: Phase 2 pump control ==");
  Serial.printf("Calibration dry=%d wet=%d. Type 'help'.\n", soilDry, soilWet);
}

void loop() {
  pollSerial();

  unsigned long now = millis();
  if (now - lastSensorMs >= SERIAL_PERIOD_MS) {
    lastSensorMs = now;
    readSensors();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    Serial.printf("soil_raw=%d soil=%.1f%% soil_ok=%d | temp=%.1fC hum=%.1f%% dht_ok=%d (%s) | tank=%s | pump=%s mode=%s reason=%s\n",
                  soilRaw, soilPct, soilOk, temp, hum, dhtOk, dhtStatus, tankOk ? "WATER" : "EMPTY",
                  ctrl.pumpOn() ? "ON" : "OFF", ctrl.mode() == Mode::Auto ? "auto" : "manual",
                  reasonStr(ctrl.reason()));
  }

  // Control runs every loop so a lockout stops the pump without waiting for the next print.
  logEvent(ctrl.update(currentInputs()));
  setRelay(ctrl.pumpOn());
}
