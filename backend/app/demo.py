"""DEMO=1: a fake ESP32 so the dashboard can be tried without hardware or a broker.

It mimics the real controller (hysteresis, manual mode, tank and max-runtime lockouts)
and feeds the same ingest path as MQTT. Time runs fast: one tick per second.
"""
import json
import math
import random
import threading
import time

from . import db, ingest

_lock = threading.Lock()
_clock = {"now": None}


def _now() -> float:
    return _clock["now"] or time.time()


S = {"soil": 55.0, "pump": False, "mode": "auto", "on": 30, "off": 60, "max_run_s": 60,
     "cooldown_s": 300, "tank_ok": True, "reason": "AUTO_TARGET_REACHED",
     "run_s": 0, "cool_s": 0, "t": 0}


def _decide() -> None:
    if not S["tank_ok"]:
        S["pump"], S["reason"] = False, "LOCKOUT_TANK_EMPTY"
    elif S["cool_s"] > 0:
        S["pump"], S["reason"] = False, "LOCKOUT_MAX_RUNTIME"
    elif S["pump"] and S["run_s"] >= S["max_run_s"]:
        S["pump"], S["reason"], S["cool_s"] = False, "LOCKOUT_MAX_RUNTIME", S["cooldown_s"]
        db.insert_event(_now(), "alert", "Max run time reached", {"reason": S["reason"]})
    elif S["mode"] == "auto":
        if S["soil"] < S["on"] and not S["pump"]:
            S["pump"], S["reason"] = True, "AUTO_LOW_MOISTURE"
            db.insert_event(_now(), "pump", "Pump ON", {"reason": S["reason"]})
        elif S["soil"] > S["off"] and S["pump"]:
            S["pump"], S["reason"] = False, "AUTO_TARGET_REACHED"
            db.insert_event(_now(), "pump", "Pump OFF", {"reason": S["reason"]})


def _step() -> None:
    S["t"] += 1
    if S["pump"]:
        S["soil"] += 1.2
        S["run_s"] += 1
    else:
        S["soil"] -= 0.35
        S["run_s"] = 0
    S["cool_s"] = max(0, S["cool_s"] - 1)
    S["soil"] = max(0.0, min(100.0, S["soil"]))
    _decide()


def tick() -> None:
    with _lock:
        _step()
        ingest.handle_sensors(json.dumps(_payload()))


def seed(minutes: int = 120) -> None:
    """Back-fill history (1 sample per 10 s) so the charts have something to show."""
    now = time.time()
    n = minutes * 6
    with _lock:
        for i in range(n):
            _clock["now"] = now - (n - i) * 10
            for _ in range(10):
                _step()
            db.insert_reading({**_payload(), "ts": _clock["now"]})
        _clock["now"] = None


def _payload() -> dict:
    return {"ts": 0, "soil": round(S["soil"] + random.uniform(-0.3, 0.3), 1),
            "soil_raw": int(3200 - S["soil"] * 18),
            "temp": round(28 + 3 * math.sin(S["t"] / 120) + random.uniform(-0.2, 0.2), 1),
            "hum": round(62 + 8 * math.cos(S["t"] / 150) + random.uniform(-0.5, 0.5), 1),
            "tank_ok": S["tank_ok"], "pump": S["pump"], "mode": S["mode"],
            "reason": S["reason"], "sensor_ok": True}


def _status() -> dict:
    return {**_payload(), "thresholds": {k: S[k] for k in ("on", "off", "max_run_s", "cooldown_s")},
            "uptime_s": S["t"], "rssi": -58}


def handle(method: str, path: str, body: dict | None) -> tuple[int, dict]:
    body = body or {}
    with _lock:
        if path == "/status":
            return 200, _status()
        if path == "/mode":
            S["mode"] = body["mode"]
            S["pump"] = False
            S["reason"] = "MANUAL_OFF" if S["mode"] == "manual" else "AUTO_TARGET_REACHED"
            return 200, _status()
        if path == "/pump":
            if S["mode"] != "manual":
                return 409, {"error": "Pump can only be commanded in manual mode"}
            want = body["state"] == "on"
            if want and not S["tank_ok"]:
                return 409, {"error": "Tank empty", "reason": "LOCKOUT_TANK_EMPTY"}
            if want and S["cool_s"] > 0:
                return 409, {"error": "Cooling down", "reason": "LOCKOUT_MAX_RUNTIME"}
            S["pump"], S["reason"] = want, "MANUAL_ON" if want else "MANUAL_OFF"
            db.insert_event(_now(), "pump", f"Pump {'ON' if want else 'OFF'}", {"reason": S["reason"]})
            return 200, _status()
        if path == "/thresholds":
            S.update({k: body[k] for k in ("on", "off", "max_run_s", "cooldown_s")})
            return 200, _status()
    return 404, {"error": "not found"}


def start() -> None:
    seed()

    def loop():
        while True:
            tick()
            time.sleep(1)
    threading.Thread(target=loop, daemon=True).start()
