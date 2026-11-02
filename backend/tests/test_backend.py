import json
import time

import pytest
from fastapi.testclient import TestClient

from app import alerts, config, db, device, ingest


@pytest.fixture()
def client(tmp_path, monkeypatch):
    monkeypatch.setattr(config, "DB_PATH", str(tmp_path / "t.db"))
    monkeypatch.setattr(config, "DEMO", False)
    db.init()
    ingest.reset_liveness()
    from app.main import app
    # skip lifespan (no MQTT thread in tests)
    return TestClient(app)


def sensors(**kw):
    p = {"ts": 0, "soil": 42.5, "soil_raw": 2210, "temp": 28.4, "hum": 61.0, "tank_ok": True,
         "pump": False, "mode": "auto", "reason": "AUTO_TARGET_REACHED", "sensor_ok": True}
    p.update(kw)
    return json.dumps(p)


# ---- parsing and storage ----
def test_store_and_latest(client):
    assert ingest.handle_sensors(sensors())
    r = client.get("/api/latest").json()
    assert r["reading"]["soil"] == 42.5
    assert r["reading"]["pump"] is False
    assert r["online"] is True


def test_bad_messages_are_rejected(client):
    assert not ingest.handle_sensors(b"not json")
    assert not ingest.handle_sensors(json.dumps({"hello": 1}))
    assert client.get("/api/latest").json()["reading"] is None


def test_uptime_ts_replaced_by_arrival_time(client):
    before = time.time()
    ingest.handle_sensors(sensors(ts=1234))  # uptime, not epoch
    assert db.latest_reading()["ts"] >= before


def test_events_stored(client):
    ingest.handle_event(json.dumps({"type": "pump", "message": "Pump ON", "reason": "AUTO_LOW_MOISTURE"}))
    ev = client.get("/api/events").json()
    assert ev[0]["type"] == "pump" and ev[0]["payload"]["reason"] == "AUTO_LOW_MOISTURE"


def test_history_downsamples(client):
    now = time.time()
    for i in range(200):
        db.insert_reading({"ts": now - 200 + i, "soil": i % 100, "tank_ok": True, "pump": False,
                           "mode": "auto", "sensor_ok": True})
    assert len(client.get("/api/history?hours=1&limit=500").json()) == 200
    assert len(client.get("/api/history?hours=1&limit=20").json()) <= 40


# ---- liveness and alerts ----
def test_lwt_offline(client):
    ingest.handle_sensors(sensors())
    ingest.handle_status("offline")
    assert client.get("/api/latest").json()["online"] is False
    ingest.handle_status("online")
    assert client.get("/api/latest").json()["online"] is True


def test_alert_derivation():
    now = 1000.0
    ok = {"tank_ok": True, "sensor_ok": True, "reason": "AUTO_TARGET_REACHED"}
    assert alerts.derive_alerts(ok, "online", now - 5, now, 30) == []
    codes = lambda a: [x["code"] for x in a]
    assert codes(alerts.derive_alerts({**ok, "tank_ok": False}, "online", now, now, 30)) == ["TANK_EMPTY"]
    assert codes(alerts.derive_alerts({**ok, "sensor_ok": False}, "online", now, now, 30)) == ["SENSOR_FAILURE"]
    assert codes(alerts.derive_alerts({**ok, "reason": "LOCKOUT_MAX_RUNTIME"}, "online", now, now, 30)) == ["MAX_RUNTIME"]
    # silent for >30s, or LWT offline
    assert codes(alerts.derive_alerts(ok, "online", now - 31, now, 30)) == ["DEVICE_OFFLINE"]
    assert codes(alerts.derive_alerts(ok, "offline", now, now, 30)) == ["DEVICE_OFFLINE"]
    assert codes(alerts.derive_alerts(None, "unknown", 0, now, 30)) == ["DEVICE_OFFLINE"]


# ---- command validation and forwarding ----
@pytest.mark.parametrize("path,body", [
    ("/api/pump", {"state": "maybe"}),
    ("/api/mode", {"mode": "turbo"}),
    ("/api/thresholds", {"on": 60, "off": 30, "max_run_s": 60, "cooldown_s": 300}),
    ("/api/thresholds", {"on": 30, "off": 30, "max_run_s": 60, "cooldown_s": 300}),
    ("/api/thresholds", {"on": -1, "off": 60, "max_run_s": 60, "cooldown_s": 300}),
    ("/api/thresholds", {"on": 30, "off": 60, "max_run_s": 0, "cooldown_s": 300}),
])
def test_invalid_commands_rejected_before_forwarding(client, monkeypatch, path, body):
    monkeypatch.setattr(device, "request", lambda *a, **k: pytest.fail("must not reach device"))
    assert client.post(path, json=body).status_code == 422


def test_forwarding_returns_device_response(client, monkeypatch):
    calls = []

    def fake(method, path, body=None):
        calls.append((method, path, body))
        return 200, {"pump": True, "reason": "MANUAL_ON"}

    monkeypatch.setattr(device, "request", fake)
    r = client.post("/api/pump", json={"state": "on"})
    assert r.status_code == 200 and r.json()["reason"] == "MANUAL_ON"
    assert calls == [("POST", "/pump", {"state": "on"})]


def test_device_409_passed_through(client, monkeypatch):
    monkeypatch.setattr(device, "request", lambda *a, **k: (409, {"error": "manual mode only"}))
    r = client.post("/api/pump", json={"state": "on"})
    assert r.status_code == 409 and r.json()["error"] == "manual mode only"


def test_device_unreachable_is_504(client, monkeypatch):
    def boom(*a, **k):
        raise device.DeviceUnreachable("timeout")

    monkeypatch.setattr(device, "request", boom)
    assert client.post("/api/mode", json={"mode": "auto"}).status_code == 504


def test_thresholds_saved_after_success(client, monkeypatch):
    monkeypatch.setattr(device, "request", lambda *a, **k: (200, {"ok": True}))
    body = {"on": 25, "off": 70, "max_run_s": 90, "cooldown_s": 120}
    assert client.post("/api/thresholds", json=body).status_code == 200
    assert client.get("/api/latest").json()["thresholds"] == body


def test_command_response_updates_latest_immediately(client, monkeypatch):
    ingest.handle_sensors(sensors(mode="auto", pump=False))
    status = json.loads(sensors(mode="manual", pump=True, reason="MANUAL_ON"))
    monkeypatch.setattr(device, "request", lambda *a, **k: (200, status))
    assert client.post("/api/pump", json={"state": "on"}).status_code == 200
    latest = client.get("/api/latest").json()["reading"]
    assert latest["pump"] is True and latest["mode"] == "manual"


def test_refused_command_does_not_store_a_reading(client, monkeypatch):
    ingest.handle_sensors(sensors(mode="auto", pump=False))
    monkeypatch.setattr(device, "request", lambda *a, **k: (409, {"error": "manual mode only", "soil": 1}))
    client.post("/api/pump", json={"state": "on"})
    assert client.get("/api/latest").json()["reading"]["pump"] is False
