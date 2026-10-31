"""Turns MQTT messages (or demo data) into database rows and tracks device liveness."""
import json
import threading
import time

from . import db

_state = {"last_seen": 0.0, "status": "unknown"}  # status: online | offline | unknown
_state_lock = threading.Lock()


def liveness() -> dict:
    with _state_lock:
        return dict(_state)


def reset_liveness() -> None:
    with _state_lock:
        _state.update(last_seen=0.0, status="unknown")


def _touch() -> None:
    with _state_lock:
        _state["last_seen"] = time.time()


def _server_ts(payload: dict) -> float:
    """Device ts is epoch when NTP worked, else uptime. Uptime is useless for charts, so use arrival time."""
    ts = payload.get("ts")
    if isinstance(ts, (int, float)) and ts > 1_000_000_000:
        return float(ts)
    return time.time()


def handle_sensors(raw: bytes | str) -> bool:
    try:
        p = json.loads(raw)
        if not isinstance(p, dict) or "soil" not in p:
            return False
    except (ValueError, TypeError):
        return False
    p["ts"] = _server_ts(p)
    db.insert_reading(p)
    _touch()
    with _state_lock:
        if _state["status"] != "offline":  # an explicit LWT 'offline' stays until the device says 'online'
            _state["status"] = "online"
    return True


def handle_event(raw: bytes | str) -> bool:
    try:
        p = json.loads(raw)
        if not isinstance(p, dict):
            return False
    except (ValueError, TypeError):
        return False
    type_ = str(p.get("type") or p.get("event") or "event")
    message = str(p.get("message") or p.get("reason") or type_)
    db.insert_event(_server_ts(p), type_, message, p)
    _touch()
    return True


def handle_status(raw: bytes | str) -> None:
    text = raw.decode() if isinstance(raw, bytes) else raw
    text = text.strip().lower()
    if text in ("online", "offline"):
        with _state_lock:
            _state["status"] = text
            if text == "online":
                _state["last_seen"] = time.time()
