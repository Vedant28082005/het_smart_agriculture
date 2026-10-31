# Report notes

## Problem and goal
Automate irrigation for a small field/pot setup: measure soil moisture, temperature and humidity (and tank level when a float switch is fitted), water automatically, and let the user monitor and override from a web dashboard.

## Architecture
```
ESP32 --MQTT publish--> Mosquitto --> FastAPI (paho subscriber) --> SQLite
ESP32 <--HTTP command-- FastAPI <--REST-- React dashboard
```
- **ESP32 (Arduino C++)**: reads sensors, decides and drives the pump itself. Publishes readings/events/status over MQTT and serves a small authenticated HTTP API for commands.
- **MQTT** carries telemetry (many small messages; the broker announces a dropped device through the Last Will). **HTTP** carries commands because they need an immediate success or refusal answer.
- **Backend (FastAPI + SQLite)** stores history, derives alerts, validates and forwards commands. **Dashboard (React + Recharts)** polls the backend.

## Key design decisions
1. **Safety logic on the device.** A Wi-Fi, broker or laptop failure must not leave the pump stuck on or stop automatic control.
2. **Hardware-independent control code** (`controller.h`; time and readings are passed in) so it is unit-tested on a PC.
3. **Hysteresis** (ON below 30 %, OFF above 60 %) prevents relay chatter.
4. **Lockouts** that commands cannot bypass: tank empty, soil sensor failure (auto mode), max run time followed by a cooldown.
5. **Fail safe**: relay OFF at boot; invalid soil readings (floating or shorted) stop automatic watering rather than guessing.
6. **No secrets in git**: `secrets.h` and `.env` are gitignored, with committed examples.
7. **Demo mode** in the backend simulates the device so the dashboard can be developed and shown without hardware.
8. Soil sensor on ADC1 (GPIO34) because ADC2 does not work while Wi-Fi is on. Readings are averaged over 10 samples and mapped linearly between calibrated dry and wet points.

## Testing
- Firmware logic: C++ unit tests in `firmware/tests` (controller 99 checks, MQTT payload/backoff 20, HTTP helpers 39): hysteresis, each lockout, cooldown, millis rollover, manual rules, threshold validation.
- Backend: 17 pytest tests (parsing, storage, downsampling, alerts, validation, forwarding including 409 and 504).
- Dashboard: type-checked production build; exercised manually against demo mode, including phone width.
- Hardware integration: the `docs/integration.md` checklist. Record the results below.

| Check | Result | Date |
|---|---|---|
| Soil calibration values (dry / wet) | | |
| Relay polarity | | |
| Auto on/off thresholds observed | | |
| Max-run lockout observed | | |
| Broker restart / Wi-Fi drop recovery | | |
| Dashboard command round trip | | |

## Limitations
- No float switch: dry running is limited only by max run time and cooldown.
- DHT11 has low accuracy (about +-2 C, +-5 % RH, whole-number resolution).
- Soil probes drift with soil type and age; recalibrate.
- Plain HTTP with a shared API key, and simple MQTT credentials: LAN only.
- Events raised while the broker is unreachable are not queued.
- A firmware hang with the pump on would leave the relay on.

## Future work
TLS for MQTT/HTTP, per-device credentials, flow or current sensing to detect a dry pump, queued offline events, multiple zones, OTA updates, a hardware relay timer.
