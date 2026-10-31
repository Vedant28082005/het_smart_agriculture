// All hardware and tuning defaults live here.
#pragma once

// ---- Pins (ESP32 DevKit V1) ----
#define PIN_SOIL_AOUT   34   // ADC1 only; ADC2 breaks when Wi-Fi is on
#define PIN_DHT         4
#define ENABLE_FLOAT_SWITCH 0  // 0 = no float switch fitted: tank is assumed to have water
#define PIN_FLOAT       27   // to GND, INPUT_PULLUP
#define PIN_RELAY       26
#define PIN_STATUS_LED  2
#define ENABLE_BUZZER   0    // set to 1 to use the buzzer
#define PIN_BUZZER      25

// ---- Sensor type: 1 = DHT22, 0 = DHT11 ----
#define USE_DHT22       0

// ---- Relay polarity (unknown yet: bench-test in docs/wiring.md) ----
#define RELAY_ACTIVE_LOW 1

// ---- Float switch: level read while the tank HAS water ----
#define FLOAT_WATER_LEVEL LOW   // LOW = switch closed to GND = water present

// ---- Soil sensor ----
#define SOIL_SAMPLES        10
#define SOIL_RAW_MIN_VALID  50     // below = wire off / short
#define SOIL_RAW_MAX_VALID  4090   // above = floating / open
#define SOIL_DEFAULT_DRY    3200   // replaced by calibration (NVS)
#define SOIL_DEFAULT_WET    1400

// ---- Timing (ms) ----
#define SERIAL_PERIOD_MS    2000
#define DHT_FAIL_TIMEOUT_MS 30000  // DHT failing this long => sensor_ok=false

#define SERIAL_BAUD         115200
