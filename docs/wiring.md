# Wiring and calibration (Phase 1)

Board: ESP32 DevKit V1. Pins are defined in `firmware/irrigation/config.h`.

| Part | Part pin | ESP32 pin | Notes |
|---|---|---|---|
| Capacitive soil sensor | VCC | 3V3 | Power from 3.3V, never 5V into the ADC |
| | GND | GND | |
| | AOUT | GPIO34 | ADC1 only (ADC2 breaks when Wi-Fi is on) |
| DHT11 (or DHT22, see `USE_DHT22`) | VCC | 3V3 | |
| | DATA | GPIO4 | 10k pull-up to 3V3 if bare sensor (not needed on a 3-pin module) |
| | GND | GND | |
| Float switch (optional; skip if not fitted, `ENABLE_FLOAT_SWITCH 0`) | wire 1 | GPIO27 | `INPUT_PULLUP` |
| | wire 2 | GND | Closed = LOW = water present (change `FLOAT_WATER_LEVEL` if reversed) |
| Relay module | VCC | VIN (5V) | |
| | GND | GND | |
| | IN | GPIO26 | Polarity set by `RELAY_ACTIVE_LOW` |
| Status LED | built-in | GPIO2 | |
| Buzzer (optional) | + | GPIO25 | Enable with `ENABLE_BUZZER` |

Pump circuit (Phase 2): separate DC supply, + through relay COM/NO to the pump, flyback diode across the pump
(cathode to +).
Never drive the pump from a GPIO. Keep water away from the ESP32, relay and wiring. If anything mains-powered is involved,
get qualified supervision. If pump switching resets the ESP32, add a 100-470 uF capacitor near it.

## Arduino IDE setup
1. Boards Manager: install "esp32 by Espressif Systems". Select board "ESP32 Dev Module".
2. Library Manager: install "DHT sensor library for ESPx" (DHTesp).
3. Open `firmware/irrigation/irrigation.ino`, upload, open Serial Monitor at 115200 baud, line ending "Newline".

## Relay polarity test (polarity unknown)
Do this with the pump disconnected. At boot the relay should be OFF (no click). If it clicks ON at boot,
flip `RELAY_ACTIVE_LOW` in `config.h` and re-upload.

## Calibration
1. Sensor in air (dry), wiped clean: type `cal dry`.
2. Sensor in a glass of water up to the marked line (not past the electronics): type `cal wet`.
3. `cal show` prints stored values; `cal reset` restores defaults. Values persist in NVS across reboots.
4. Soil % is a linear map between dry and wet, clamped 0-100. For real soil, also check a dry-soil and a
   freshly watered reading and note them.

Serial output every 2 s, for example:
`soil_raw=2210 soil=55.0% soil_ok=1 | temp=28.4C hum=61.0% dht_ok=1 | tank=WATER | sensor_ok=1`

Failure handling: a soil raw outside 50-4090 sets `soil_ok=0`; if the DHT has no good read for 30 s `dht_ok=0`
(last good values are kept and shown).
