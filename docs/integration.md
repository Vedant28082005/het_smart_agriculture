# Integration checklist

Work top to bottom. Do not move on until a step passes. Keep the pump DISCONNECTED (relay only, or an LED on the relay output) until step 6.

## 1. Bench: each part alone
- [ ] Soil sensor: raw value steady (not stuck at 0/4095) and changes when wet/dry. `soil_ok=1`.
- [ ] `cal dry` (in air) and `cal wet` (in water to the line) done; `cal show` prints both.
- [ ] DHT11 gives temperature/humidity, `dht_ok=1`.
- [ ] Relay is OFF at power-up and when the board resets. Polarity correct (`RELAY_ACTIVE_LOW`), see `docs/wiring.md`.

## 2. Control logic on the ESP32 (serial only, no network)
- [ ] `mode manual`, `pump on` clicks the relay; `pump off` releases it.
- [ ] `pump on` in auto mode is refused.
- [ ] `mode auto`: dry the sensor, relay turns on below the ON threshold; wet it, relay turns off above OFF.
- [ ] Max run time: pump stops after `max_run_s` with `LOCKOUT_MAX_RUNTIME`, and does not restart until the cooldown ends.
- [ ] Unplug the soil sensor signal wire: pump stays off in auto, reason `LOCKOUT_SENSOR_FAIL`.
- [ ] `th 20 50 30 60`, then reset the board: `th show` still shows the new values.

## 3. Network
- [ ] `secrets.h` filled in (never committed). Serial shows `WiFi up, ip=...` and `MQTT connected`.
- [ ] `mosquitto_sub -h localhost -t "farm/field1/#" -v` shows sensors every 5 s and `status online`.
- [ ] Power off the ESP32: `status offline` appears (Last Will, can take around 15 s).
- [ ] Stop Mosquitto, wait, restart it: the ESP32 reconnects by itself, and pump control kept working meanwhile.
- [ ] Turn the router/hotspot off and on: same.

## 4. HTTP API (curl, see `docs/api.md`)
- [ ] No key -> 401; wrong key -> 401; right key -> 200.
- [ ] `/pump` in auto -> 409; after `/mode manual` -> 200.
- [ ] Bad thresholds (`on` >= `off`) -> 400.

## 5. Backend + dashboard against the real device
- [ ] `backend/.env`: `DEMO=0`, MQTT and `DEVICE_*` set. `GET /api/latest` shows `online: true`.
- [ ] Dashboard shows live values; the badge goes "Device offline" within ~30 s of unplugging the ESP32.
- [ ] Toggle mode, pump and thresholds from the dashboard; the serial monitor shows the change.
- [ ] Stop the backend: the pump logic on the ESP32 is unaffected.

## 6. Pump, last
- [ ] Separate supply for the pump; the relay switches the pump's supply; flyback diode across a DC pump.
- [ ] Water source and pump are away from the ESP32, breadboard and wiring (drip loops, no joints under the tank).
- [ ] First run with a short `max_run_s` (e.g. 10 s) and watch it.
- [ ] Mains-powered pumps only with qualified supervision.

## Known limits to remember
- No float switch fitted: only max run time + cooldown protect against dry running.
- If the firmware hangs while the pump is on, the relay stays on. Keep `max_run_s` small and consider a hardware timer.
- The HTTP API is plain HTTP with a shared key, for a trusted LAN only.
