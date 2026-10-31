# ESP32 HTTP API (Phase 4)

Served by the ESP32 on port 80 (built-in `WebServer`, no extra library). The backend calls it; the dashboard never does.
Every route needs the header `X-API-Key: <API_KEY from secrets.h>`, otherwise `401`. Plain HTTP, so use it on a trusted LAN only.
Bodies are small JSON (max 256 bytes). A bad body gives `400`; a refused command gives `409`.

| Method | Path | Body | Success |
|---|---|---|---|
| GET | `/status` | none | `200` status object |
| POST | `/pump` | `{"state":"on"\|"off"}` | `200` status object |
| POST | `/mode` | `{"mode":"auto"\|"manual"}` | `200` status object |
| POST | `/thresholds` | `{"on":30,"off":60,"max_run_s":60,"cooldown_s":300}` (all four, integers) | `200` status object, saved to flash |

Status object: the MQTT sensors fields (`ts, soil, soil_raw, temp, hum, tank_ok, pump, mode, reason, sensor_ok, rssi`)
plus `thresholds:{on,off,max_run_s,cooldown_s}` and `uptime_s`.

Errors are `{"error":"...","reason":"..."}` (`reason` only for lockouts):
| Code | When |
|---|---|
| 400 | bad/missing JSON, value out of range (`0<=on<off<=100`, `1<=max_run_s<=3600`, `0<=cooldown_s<=86400`) |
| 401 | missing or wrong API key |
| 404 | unknown path |
| 409 | `/pump` while in auto mode; tank empty (`LOCKOUT_TANK_EMPTY`); cooling down (`LOCKOUT_MAX_RUNTIME`) |

Safety lockouts are enforced on the ESP32 and cannot be bypassed from the API.

## curl examples
```bash
KEY=change-me; ESP=http://192.168.1.50
curl -H "X-API-Key: $KEY" $ESP/status
curl -H "X-API-Key: $KEY" -d '{"mode":"manual"}' $ESP/mode
curl -H "X-API-Key: $KEY" -d '{"state":"on"}'   $ESP/pump
curl -H "X-API-Key: $KEY" -d '{"on":25,"off":65,"max_run_s":45,"cooldown_s":120}' $ESP/thresholds
```
Backend: set `DEVICE_HOST` (the ESP32's IP, shown on the serial monitor as `WiFi up, ip=...`) and `DEVICE_API_KEY` in `backend/.env`.
