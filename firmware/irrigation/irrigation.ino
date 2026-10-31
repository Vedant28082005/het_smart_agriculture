// Smart irrigation, Phase 1: sensor bench test + calibration.
// Libraries (Arduino IDE Library Manager): "DHT sensor library for ESPx" (DHTesp).
// Board: "ESP32 Dev Module".
#include <Preferences.h>
#include <DHTesp.h>
#include "config.h"
#include "src/irrigation_logic/soil_calc.h"

Preferences prefs;
DHTesp dht;

int soilDry = SOIL_DEFAULT_DRY;
int soilWet = SOIL_DEFAULT_WET;

int soilRaw = 0;
float soilPct = 0;
bool soilOk = false;

float temp = NAN, hum = NAN;
bool dhtOk = false;
unsigned long lastDhtGoodMs = 0;

bool tankOk = false;

unsigned long lastPrintMs = 0;
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
  } else if (c == "help") {
    Serial.println("Commands: cal dry | cal wet | cal show | cal reset | help");
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
  dhtOk = (lastDhtGoodMs != 0) && (millis() - lastDhtGoodMs < DHT_FAIL_TIMEOUT_MS);

#if ENABLE_FLOAT_SWITCH
  tankOk = (digitalRead(PIN_FLOAT) == FLOAT_WATER_LEVEL);
#else
  tankOk = true;  // no float switch: tank level is not monitored
#endif
}

void setup() {
  // Relay OFF first, before anything else (Phase 2 will drive it properly).
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_ACTIVE_LOW ? HIGH : LOW);

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

  Serial.println("\n== Smart Irrigation: Phase 1 bench test ==");
  Serial.printf("Calibration dry=%d wet=%d. Type 'help'.\n", soilDry, soilWet);
}

void loop() {
  pollSerial();

  unsigned long now = millis();
  if (now - lastPrintMs >= SERIAL_PERIOD_MS) {
    lastPrintMs = now;
    readSensors();
    digitalWrite(PIN_STATUS_LED, !digitalRead(PIN_STATUS_LED));
    Serial.printf("soil_raw=%d soil=%.1f%% soil_ok=%d | temp=%.1fC hum=%.1f%% dht_ok=%d | tank=%s | sensor_ok=%d\n",
                  soilRaw, soilPct, soilOk, temp, hum, dhtOk,
                  tankOk ? "WATER" : "EMPTY", soilOk && dhtOk);
  }
}
