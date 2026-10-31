# MQTT (Phase 3)

Library: **PubSubClient** (Arduino IDE Library Manager, by Nick O'Leary). Broker: Mosquitto on the laptop.

## Topics (prefix `farm/field1`)
| Topic | Direction | Retained | Payload |
|---|---|---|---|
| `farm/field1/sensors` | ESP32 -> broker, every 5 s | no | `{"ts","soil","soil_raw","temp","hum","tank_ok","pump","mode","reason","sensor_ok","rssi"}` |
| `farm/field1/events` | ESP32 -> broker, on change | no | `{"ts","type":"pump"\|"alert","message","reason"}` |
| `farm/field1/status` | ESP32 -> broker | yes | `online` (on connect), `offline` (Last Will when the link drops) |

`ts` is epoch seconds once NTP has synced, otherwise 0 and the backend stamps arrival time.
Commands do NOT go over MQTT; they use the HTTP API (Phase 4).

## Reconnect behaviour
Wi-Fi retries every 10 s doubling to 60 s; MQTT retries 1 s doubling to 60 s. The socket timeout is 2 s.
The pump control loop keeps running with Wi-Fi or the broker down. Events raised while offline are not queued;
the next `sensors` message carries the current state.

## Setup
1. Copy `firmware/irrigation/secrets.example.h` to `secrets.h` and fill in Wi-Fi and broker details (`MQTT_HOST` = laptop IP).
2. Mosquitto (laptop), `mosquitto.conf` for a LAN-only test:
   ```
   listener 1883
   allow_anonymous true
   ```
   For anything beyond a bench test, create a user with `mosquitto_passwd -c passwd irrigation`, set
   `allow_anonymous false` and `password_file passwd`, and put the same user/password in `secrets.h` and `backend/.env`.
   Allow port 1883 through the Windows firewall for private networks.
3. Watch traffic: `mosquitto_sub -h localhost -t "farm/field1/#" -v`
4. Backend: set `DEMO=0`, `MQTT_HOST`, `MQTT_USER`, `MQTT_PASS` in `backend/.env`.

## Prototype shortcut
`mosquitto/mosquitto.conf` (anonymous, LAN) is ready: `mosquitto -c mosquitto/mosquitto.conf -v`.
The ESP32 announces itself as `irrigation.local`, which `backend/.env` already uses as `DEVICE_HOST`.
