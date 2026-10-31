// Host-side tests for the HTTP API helpers.
#include <cstdio>
#include <cstring>
#include "../irrigation/src/irrigation_logic/api.h"

static int failed = 0, total = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    total++;                                                          \
    if (!(cond)) { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static void json_readers() {
  long v = 0;
  CHECK(jsonGetInt("{\"on\":30,\"off\": 60}", "on", &v) && v == 30);
  CHECK(jsonGetInt("{\"on\":30,\"off\": 60}", "off", &v) && v == 60);
  CHECK(jsonGetInt("{ \"on\" : -5 }", "on", &v) && v == -5);
  CHECK(!jsonGetInt("{\"on\":30}", "off", &v));
  CHECK(!jsonGetInt("{\"on\":\"30\"}", "on", &v));      // string, not a number
  CHECK(!jsonGetInt("{\"on\":30.5}", "on", &v));        // fractions refused
  CHECK(!jsonGetInt("{\"on\":}", "on", &v));
  CHECK(!jsonGetInt("", "on", &v));
  char s[16];
  CHECK(jsonGetStr("{\"state\":\"on\"}", "state", s, sizeof s) && !strcmp(s, "on"));
  CHECK(jsonGetStr("{\"mode\" : \"manual\"}", "mode", s, sizeof s) && !strcmp(s, "manual"));
  CHECK(!jsonGetStr("{\"state\":on}", "state", s, sizeof s));
  CHECK(!jsonGetStr("{\"state\":\"averyveryverylongvalue\"}", "state", s, sizeof s));
  CHECK(!jsonGetStr("{}", "state", s, sizeof s));
}

static void api_key() {
  CHECK(apiKeyOk("secret", "secret"));
  CHECK(!apiKeyOk("secreT", "secret"));
  CHECK(!apiKeyOk("secre", "secret"));
  CHECK(!apiKeyOk("secrets", "secret"));
  CHECK(!apiKeyOk("", "secret"));
  CHECK(!apiKeyOk(nullptr, "secret"));
  CHECK(!apiKeyOk("", ""));          // unset key never authorises
  CHECK(!apiKeyOk("x", ""));
}

static void thresholds_parse() {
  Thresholds t;
  CHECK(parseThresholds("{\"on\":25,\"off\":70,\"max_run_s\":45,\"cooldown_s\":120}", &t));
  CHECK(t.on == 25 && t.off == 70 && t.maxRunS == 45 && t.cooldownS == 120 && thresholdsValid(t));
  CHECK(!parseThresholds("{\"on\":25,\"off\":70,\"max_run_s\":45}", &t));  // missing field
  CHECK(!parseThresholds("{\"on\":25,\"off\":70,\"max_run_s\":-1,\"cooldown_s\":1}", &t));
  Thresholds bad;
  CHECK(parseThresholds("{\"on\":80,\"off\":70,\"max_run_s\":45,\"cooldown_s\":120}", &bad));
  CHECK(!thresholdsValid(bad));      // parsed fine, rejected by the rule check
}

static void status_json() {
  SensorSnapshot s{};
  s.ts = 100; s.soil = 40; s.soilRaw = 2000; s.temp = 27; s.hum = 50; s.tankOk = true;
  s.mode = Mode::Manual; s.reason = Reason::ManualOff; s.sensorOk = true; s.rssi = -60;
  char b[400];
  int n = buildStatusJson(b, sizeof b, s, Thresholds(), 1234);
  CHECK(n > 0 && (size_t)n == strlen(b));
  CHECK(b[0] == '{' && b[n - 1] == '}');
  CHECK(strstr(b, "\"thresholds\":{\"on\":30,\"off\":60,\"max_run_s\":60,\"cooldown_s\":300}"));
  CHECK(strstr(b, "\"uptime_s\":1234"));
  CHECK(strstr(b, "\"mode\":\"manual\""));
  char tiny[120];
  CHECK(buildStatusJson(tiny, sizeof tiny, s, Thresholds(), 1) == -1);
}

static void errors_and_codes() {
  char b[100];
  buildErrorJson(b, sizeof b, "Tank empty", "LOCKOUT_TANK_EMPTY");
  CHECK(!strcmp(b, "{\"error\":\"Tank empty\",\"reason\":\"LOCKOUT_TANK_EMPTY\"}"));
  buildErrorJson(b, sizeof b, "bad");
  CHECK(!strcmp(b, "{\"error\":\"bad\"}"));
  CHECK(cmdResultHttp(CmdResult::Ok).status == 200);
  CHECK(cmdResultHttp(CmdResult::NotManual).status == 409);
  CHECK(cmdResultHttp(CmdResult::TankEmpty).status == 409);
  CHECK(!strcmp(cmdResultHttp(CmdResult::CoolingDown).reason, "LOCKOUT_MAX_RUNTIME"));
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "print")) {  // sample status for backend cross-check
    SensorSnapshot s{};
    s.ts = 1760000000; s.soil = 40; s.soilRaw = 2000; s.temp = 27; s.hum = 50; s.tankOk = true;
    s.mode = Mode::Manual; s.reason = Reason::ManualOff; s.sensorOk = true; s.rssi = -60;
    char b[400];
    buildStatusJson(b, sizeof b, s, Thresholds(), 1234);
    puts(b);
    return 0;
  }
  json_readers();
  api_key();
  thresholds_parse();
  status_json();
  errors_and_codes();
  printf("%d checks, %d failed\n", total, failed);
  return failed ? 1 : 0;
}
