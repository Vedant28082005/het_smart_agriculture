// Host-side tests for MQTT payloads and reconnect backoff.
// With an argument "print" it also prints sample payloads (used to check them against the backend).
#include <cstdio>
#include <cstring>
#include "../irrigation/src/irrigation_logic/payload.h"
#include "../irrigation/src/irrigation_logic/backoff.h"

static int failed = 0, total = 0;
#define CHECK(cond)                                                   \
  do {                                                                \
    total++;                                                          \
    if (!(cond)) { failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
  } while (0)

static SensorSnapshot snap() {
  SensorSnapshot s{};
  s.ts = 1760000000; s.soil = 42.34f; s.soilRaw = 2100; s.temp = 28.8f; s.hum = 64.0f;
  s.tankOk = true; s.pump = false; s.mode = Mode::Auto; s.reason = Reason::AutoTargetReached;
  s.sensorOk = true; s.rssi = -58;
  return s;
}

static void sensors_json() {
  char b[256];
  int n = buildSensorsJson(b, sizeof b, snap());
  CHECK(n > 0 && (size_t)n == strlen(b));
  CHECK(strstr(b, "\"soil\":42.3,") != nullptr);
  CHECK(strstr(b, "\"pump\":false") != nullptr);
  CHECK(strstr(b, "\"mode\":\"auto\"") != nullptr);
  CHECK(strstr(b, "\"reason\":\"AUTO_TARGET_REACHED\"") != nullptr);
  CHECK(strstr(b, "\"tank_ok\":true") != nullptr);
  CHECK(b[0] == '{' && b[n - 1] == '}');
  char tiny[20];
  CHECK(buildSensorsJson(tiny, sizeof tiny, snap()) == -1);  // truncation is reported, not silent
}

static void event_json() {
  char b[200];
  Event e; e.kind = EventKind::PumpOn; e.reason = Reason::AutoLowMoisture;
  CHECK(buildEventJson(b, sizeof b, 5, e) > 0);
  CHECK(strstr(b, "\"type\":\"pump\"") && strstr(b, "\"message\":\"Pump ON\"") &&
        strstr(b, "\"reason\":\"AUTO_LOW_MOISTURE\""));
  e.kind = EventKind::Alert; e.reason = Reason::LockoutMaxRuntime;
  buildEventJson(b, sizeof b, 5, e);
  CHECK(strstr(b, "\"type\":\"alert\"") && strstr(b, "Max run time reached"));
}

static void backoff() {
  Backoff b(1000, 8000);
  CHECK(b.due(0));                       // first attempt immediately
  b.failed(0);
  CHECK(!b.due(999) && b.due(1000));     // 1 s
  b.failed(1000);
  CHECK(!b.due(2999) && b.due(3000));    // 2 s
  b.failed(3000);
  CHECK(b.currentDelayMs() == 4000);
  b.failed(7000);
  CHECK(b.currentDelayMs() == 8000);
  b.failed(15000);
  CHECK(b.currentDelayMs() == 8000);     // capped
  b.succeeded();
  CHECK(b.due(15001));
  b.failed(20000);
  CHECK(b.currentDelayMs() == 1000);     // restarted
  // rollover
  Backoff r(1000, 8000);
  r.failed(0xFFFFFFFFu - 100);
  CHECK(!r.due(0xFFFFFFFFu) && r.due(900));
}

int main(int argc, char **argv) {
  if (argc > 1 && !strcmp(argv[1], "print")) {
    char b[256];
    buildSensorsJson(b, sizeof b, snap());
    puts(b);
    return 0;
  }
  sensors_json();
  event_json();
  backoff();
  printf("%d checks, %d failed\n", total, failed);
  return failed ? 1 : 0;
}
