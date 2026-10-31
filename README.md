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
- [x] Phase 5: FastAPI backend (built ahead of firmware phases, tested with a mocked device)
- [x] Phase 6: React dashboard
- [x] Phase 2: control logic + pump (serial-driven; PC unit tests in `firmware/tests`, 99 checks pass)
- [x] Phase 3: Wi-Fi + MQTT publishing, Last Will, NTP (see `docs/mqtt.md`; payload/backoff tests in `firmware/tests`, not yet run on hardware)
- [ ] Phase 4 (ESP32 HTTP server) and 7 (integration)

## Try the dashboard without hardware (demo mode)
The backend has a built-in fake device (`DEMO=1`) that behaves like the planned firmware.

```bash
# terminal 1: backend
cd backend
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt   # Linux/macOS: .venv/bin/python
cp .env.example .env        # then set DEMO=1
.venv/Scripts/python -m uvicorn app.main:app --port 8000

# terminal 2: dashboard
cd dashboard
npm install
cp .env.example .env
npm run dev                 # http://localhost:5173
```

API docs: http://localhost:8000/docs. Backend tests: `cd backend && .venv/Scripts/python -m pytest`.
With `DEMO=0` the backend subscribes to Mosquitto and forwards commands to the ESP32 (`DEVICE_HOST`, `DEVICE_API_KEY` in `backend/.env`).

## Device HTTP contract the backend expects (to be built in Phase 4)
`POST /pump {"state":"on"|"off"}`, `POST /mode {"mode":"auto"|"manual"}`,
`POST /thresholds {"on","off","max_run_s","cooldown_s"}`, all with an `X-API-Key` header.
Events published on `farm/field1/events` should look like `{"type":"pump","message":"Pump ON","reason":"AUTO_LOW_MOISTURE"}`.

## Safety
GPIO is 3.3V only. Never drive the pump from a GPIO; use a relay and a separate supply with a flyback diode.
Keep water away from electronics.
