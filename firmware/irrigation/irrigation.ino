// Smart irrigation, Phase 2: sensors, calibration and pump control (serial-driven).
// Libraries (Arduino IDE Library Manager): "DHT sensor library for ESPx" (DHTesp).
// Board: "ESP32 Dev Module".
#include <Preferences.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <time.h>
#include "config.h"
#include "secrets.h"
#include "src/irrigation_logic/payload.h"
#include "src/irrigation_logic/backoff.h"
#include "src/irrigation_logic/api.h"
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
bool sensorsReady = false;  // control stays idle until the first real reading

Controller ctrl;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer server(HTTP_PORT);
Backoff wifiBackoff(WIFI_RETRY_MIN_MS, WIFI_RETRY_MAX_MS);
Backoff mqttBackoff(MQTT_RETRY_MIN_MS, MQTT_RETRY_MAX_MS);
bool wifiWasUp = false;
unsigned long lastPublishMs = 0;
const char *TOPIC_SENSORS = MQTT_PREFIX "/sensors";
const char *TOPIC_EVENTS = MQTT_PREFIX "/events";
const char *TOPIC_STATUS = MQTT_PREFIX "/status";

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
#if RELAY_ACTIVE_LOW && RELAY_OFF_HIGHZ
  // Active-low module: drive LOW to switch on, release the pin to switch off. The module's own
  // pull-up then holds IN high, even when it is powered from 5 V and the ESP32's 3.3 V HIGH
  // is too low to switch it off.
  if (on) {
    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);
  } else {
    pinMode(PIN_RELAY, INPUT);
  }
#else
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, relayLevelHigh(on, RELAY_ACTIVE_LOW) ? HIGH : LOW);
#endif
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

// Epoch seconds once NTP has synced, else 0 (the backend then stamps arrival time).
uint32_t epochNow() {
  time_t t = time(nullptr);
  return t > 1600000000 ? (uint32_t)t : 0;
}

void logEvent(const Event &e) {
  if (e.kind == EventKind::None) return;
  const char *what = e.kind == EventKind::PumpOn ? "PUMP ON" : e.kind == EventKind::PumpOff ? "PUMP OFF" : "ALERT";
  Serial.printf("EVENT %s reason=%s\n", what, reasonStr(e.reason));
  if (mqtt.connected()) {  // events raised while offline are not queued; the next sensors message carries the state
    char buf[200];
    if (buildEventJson(buf, sizeof buf, epochNow(), e) > 0) mqtt.publish(TOPIC_EVENTS, buf);
  }
}

// Never blocks for long: reconnects are paced by backoff and the socket timeout is short,
// so the control loop keeps running with Wi-Fi or the broker down.
void netLoop() {
  unsigned long now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiWasUp) {
      wifiWasUp = false;
      Serial.println("WiFi lost");
    }
    if (wifiBackoff.due(now)) {
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASS);
      wifiBackoff.failed(now);
    }
    return;
  }
  if (!wifiWasUp) {
    wifiWasUp = true;
    wifiBackoff.succeeded();
    Serial.printf("WiFi up, ip=%s rssi=%d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    if (MDNS.begin("irrigation")) Serial.println("Reachable as http://irrigation.local");
  }
  if (!mqtt.connected()) {
    if (mqttBackoff.due(now)) {
      // Last Will: the broker publishes "offline" if we drop without saying goodbye.
      bool ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, TOPIC_STATUS, 1, true, "offline");
      if (ok) {
        mqttBackoff.succeeded();
        mqtt.publish(TOPIC_STATUS, "online", true);
        Serial.println("MQTT connected");
      } else {
        mqttBackoff.failed(now);
        Serial.printf("MQTT connect failed rc=%d, retry in %lus\n", mqtt.state(),
                      (unsigned long)(mqttBackoff.currentDelayMs() / 1000));
      }
    }
    return;
  }
  mqtt.loop();
}

SensorSnapshot makeSnapshot() {
  SensorSnapshot s;
  s.ts = epochNow();
  s.soil = soilPct;
  s.soilRaw = soilRaw;
  s.temp = isnan(temp) ? 0 : temp;  // never-read DHT: 0 with sensor_ok=false
  s.hum = isnan(hum) ? 0 : hum;
  s.tankOk = tankOk;
  s.pump = ctrl.pumpOn();
  s.mode = ctrl.mode();
  s.reason = ctrl.reason();
  s.sensorOk = soilOk && dhtOk;
  s.rssi = WiFi.RSSI();
  return s;
}

void publishSensors() {
  if (!mqtt.connected()) return;
  char buf[256];
  if (buildSensorsJson(buf, sizeof buf, makeSnapshot()) > 0) mqtt.publish(TOPIC_SENSORS, buf);
}

// ---- HTTP API (see docs/api.md). Every route requires the X-API-Key header. ----
void sendJson(int code, const char *body) {
  server.send(code, "application/json", body);
}

void sendError(int code, const char *error, const char *reason = nullptr) {
  char buf[160];
  buildErrorJson(buf, sizeof buf, error, reason);
  sendJson(code, buf);
}

void sendStatus() {
  char buf[480];
  if (buildStatusJson(buf, sizeof buf, makeSnapshot(), ctrl.thresholds(), millis() / 1000) > 0) sendJson(200, buf);
  else sendError(500, "status too large");
}

// Auth + body size check shared by all routes. Returns false after sending the error response.
bool authorized() {
  if (!apiKeyOk(server.header("X-API-Key").c_str(), API_KEY)) {
    sendError(401, "Missing or invalid API key");
    return false;
  }
  return true;
}

bool bodyOk(String &body) {
  body = server.arg("plain");
  if (body.length() == 0 || body.length() > 256) {
    sendError(400, "Expected a JSON body of at most 256 bytes");
    return false;
  }
  return true;
}

void httpStatus() {
  if (authorized()) sendStatus();
}

void httpPump() {
  String body;
  if (!authorized() || !bodyOk(body)) return;
  char state[8];
  if (!jsonGetStr(body.c_str(), "state", state, sizeof state) || (strcmp(state, "on") && strcmp(state, "off"))) {
    sendError(400, "state must be on or off");
    return;
  }
  Event e;
  CmdResult r = ctrl.commandPump(!strcmp(state, "on"), currentInputs(), &e);
  setRelay(ctrl.pumpOn());
  logEvent(e);
  CmdHttp h = cmdResultHttp(r);
  if (h.status == 200) sendStatus();
  else sendError(h.status, h.error, h.reason);
}

void httpMode() {
  String body;
  if (!authorized() || !bodyOk(body)) return;
  char mode[10];
  if (!jsonGetStr(body.c_str(), "mode", mode, sizeof mode) || (strcmp(mode, "auto") && strcmp(mode, "manual"))) {
    sendError(400, "mode must be auto or manual");
    return;
  }
  ctrl.setMode(!strcmp(mode, "auto") ? Mode::Auto : Mode::Manual, millis());
  setRelay(ctrl.pumpOn());
  sendStatus();
}

void httpThresholds() {
  String body;
  if (!authorized() || !bodyOk(body)) return;
  Thresholds t;
  if (!parseThresholds(body.c_str(), &t) || !ctrl.setThresholds(t)) {
    sendError(400, "need integers 0<=on<off<=100, 1<=max_run_s<=3600, 0<=cooldown_s<=86400");
    return;
  }
  saveThresholds();
  sendStatus();
}

void startHttp() {
  const char *keys[] = {"X-API-Key"};
  server.collectHeaders(keys, 1);
  server.on("/status", HTTP_GET, httpStatus);
  server.on("/pump", HTTP_POST, httpPump);
  server.on("/mode", HTTP_POST, httpMode);
  server.on("/thresholds", HTTP_POST, httpThresholds);
  server.onNotFound([]() { sendError(404, "not found"); });
  server.begin();
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
  readSensors();
  sensorsReady = true;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  wifiBackoff.failed(millis());
  configTime(0, 0, NTP_SERVER);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setSocketTimeout(MQTT_SOCKET_TIMEOUT_S);
  mqtt.setBufferSize(512);
  startHttp();

  Serial.println("\n== Smart Irrigation: Phase 4 pump control + Wi-Fi/MQTT + HTTP API ==");
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
  if (sensorsReady) logEvent(ctrl.update(currentInputs()));
  setRelay(ctrl.pumpOn());

  server.handleClient();
  netLoop();
  if (now - lastPublishMs >= PUBLISH_PERIOD_MS) {
    lastPublishMs = now;
    publishSensors();
  }
}
