"""SQLite storage. One short-lived connection per call keeps threading simple."""
import json
import sqlite3
import threading
from contextlib import contextmanager

from . import config

_lock = threading.Lock()

SCHEMA = """
CREATE TABLE IF NOT EXISTS readings(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts REAL NOT NULL, soil REAL, soil_raw INTEGER, temp REAL, hum REAL,
  tank_ok INTEGER, pump INTEGER, mode TEXT, reason TEXT, sensor_ok INTEGER);
CREATE INDEX IF NOT EXISTS idx_readings_ts ON readings(ts);
CREATE TABLE IF NOT EXISTS events(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts REAL NOT NULL, type TEXT, message TEXT, payload TEXT);
CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT);
"""


@contextmanager
def connect():
    with _lock:
        con = sqlite3.connect(config.DB_PATH)
        con.row_factory = sqlite3.Row
        try:
            yield con
            con.commit()
        finally:
            con.close()


def init() -> None:
    with connect() as con:
        con.executescript(SCHEMA)


def insert_reading(r: dict) -> None:
    with connect() as con:
        con.execute(
            "INSERT INTO readings(ts,soil,soil_raw,temp,hum,tank_ok,pump,mode,reason,sensor_ok)"
            " VALUES(?,?,?,?,?,?,?,?,?,?)",
            (r["ts"], r.get("soil"), r.get("soil_raw"), r.get("temp"), r.get("hum"),
             int(bool(r.get("tank_ok"))), int(bool(r.get("pump"))), r.get("mode"),
             r.get("reason"), int(bool(r.get("sensor_ok", True)))),
        )


def insert_event(ts: float, type_: str, message: str, payload: dict) -> None:
    with connect() as con:
        con.execute("INSERT INTO events(ts,type,message,payload) VALUES(?,?,?,?)",
                    (ts, type_, message, json.dumps(payload)))


def latest_reading() -> dict | None:
    with connect() as con:
        row = con.execute("SELECT * FROM readings ORDER BY id DESC LIMIT 1").fetchone()
    return _reading(row) if row else None


def _reading(row) -> dict:
    d = dict(row)
    for k in ("tank_ok", "pump", "sensor_ok"):
        d[k] = bool(d[k])
    return d


def history(since: float, max_points: int) -> list[dict]:
    """Rows since `since`, averaged into time buckets if there are more than max_points."""
    with connect() as con:
        n = con.execute("SELECT COUNT(*) FROM readings WHERE ts>=?", (since,)).fetchone()[0]
        if n <= max_points:
            rows = con.execute("SELECT * FROM readings WHERE ts>=? ORDER BY ts", (since,)).fetchall()
            return [_reading(r) for r in rows]
        import time
        step = max((time.time() - since) / max_points, 1.0)
        rows = con.execute(
            "SELECT AVG(ts) ts, AVG(soil) soil, AVG(soil_raw) soil_raw, AVG(temp) temp, AVG(hum) hum,"
            " MIN(tank_ok) tank_ok, MAX(pump) pump, MAX(mode) mode, MAX(reason) reason, MIN(sensor_ok) sensor_ok"
            " FROM readings WHERE ts>=? GROUP BY CAST((ts-?)/? AS INTEGER) ORDER BY ts",
            (since, since, step)).fetchall()
        return [_reading(r) for r in rows]


def recent_events(limit: int) -> list[dict]:
    with connect() as con:
        rows = con.execute("SELECT * FROM events ORDER BY id DESC LIMIT ?", (limit,)).fetchall()
    out = []
    for r in rows:
        d = dict(r)
        d["payload"] = json.loads(d["payload"] or "{}")
        out.append(d)
    return out


def set_setting(key: str, value) -> None:
    with connect() as con:
        con.execute("INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)", (key, json.dumps(value)))


def get_setting(key: str, default=None):
    with connect() as con:
        row = con.execute("SELECT value FROM settings WHERE key=?", (key,)).fetchone()
    return json.loads(row["value"]) if row else default
