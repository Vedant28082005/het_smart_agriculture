# IoT-Based Smart Agriculture and Automated Irrigation System

ESP32 monitors soil moisture, temperature, humidity and tank level, runs the pump automatically, and exposes
a web dashboard. MQTT carries sensor data; HTTP/REST carries commands and history.

```
ESP32 --MQTT publish--> Mosquitto (laptop) --> FastAPI (paho subscriber) --> SQLite
ESP32 <--HTTP command-- FastAPI <--REST-- React dashboard
```

Irrigation and safety decisions run on the ESP32, so it keeps working if Wi-Fi, the broker or the laptop is down.

## Layout
- `firmware/irrigation/`: Arduino IDE sketch (ESP32). Hardware-independent logic in `src/irrigation_logic/`.
- `backend/`: FastAPI service (Phase 5)
- `dashboard/`: React + Vite (Phase 6)
- `docs/`: wiring, API, MQTT topics, calibration

## Status
- [x] Phase 1: scaffold + sensor bench test (see `docs/wiring.md`)
- [ ] Phases 2-7

## Safety
GPIO is 3.3V only. Never drive the pump from a GPIO; use a relay and a separate supply with a flyback diode.
Keep water away from electronics.
