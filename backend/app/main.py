import json
import time
from contextlib import asynccontextmanager
from typing import Literal

from fastapi import FastAPI, HTTPException, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field, model_validator

from . import alerts, config, db, device, ingest

DEFAULT_THRESHOLDS = {"on": 30, "off": 60, "max_run_s": 60, "cooldown_s": 300}


@asynccontextmanager
async def lifespan(app: FastAPI):
    db.init()
    if config.DEMO:
        from . import demo
        demo.start()
    else:
        from . import mqtt_client
        mqtt_client.start()
    yield


app = FastAPI(title="Smart Irrigation API", lifespan=lifespan)
app.add_middleware(CORSMiddleware, allow_origins=config.CORS_ORIGINS,
                   allow_methods=["*"], allow_headers=["*"])


# ---- command bodies (validated here, then forwarded to the ESP32) ----
class PumpCmd(BaseModel):
    state: Literal["on", "off"]


class ModeCmd(BaseModel):
    mode: Literal["auto", "manual"]


class ThresholdsCmd(BaseModel):
    on: int = Field(ge=0, le=100)
    off: int = Field(ge=0, le=100)
    max_run_s: int = Field(ge=1, le=3600)
    cooldown_s: int = Field(ge=0, le=86400)

    @model_validator(mode="after")
    def on_below_off(self):
        if self.on >= self.off:
            raise ValueError("on threshold must be lower than off threshold")
        return self


def _forward(path: str, body: BaseModel) -> JSONResponse:
    try:
        status, data = device.request("POST", path, body.model_dump())
    except device.DeviceUnreachable:
        raise HTTPException(504, "Device unreachable")
    # The device answers with its fresh status. Store it now so /api/latest reflects the
    # command immediately instead of waiting for the next MQTT sensors message (up to 5 s).
    if status < 300 and isinstance(data, dict) and "soil" in data:
        ingest.handle_sensors(json.dumps(data))
    return JSONResponse(data, status_code=status)


# ---- read endpoints ----
@app.get("/api/health")
def health():
    live = ingest.liveness()
    return {"ok": True, "demo": config.DEMO, "mqtt_status": live["status"]}


@app.get("/api/latest")
def latest():
    live = ingest.liveness()
    now = time.time()
    online = alerts.device_online(live["status"], live["last_seen"], now, config.OFFLINE_AFTER_S)
    return {
        "reading": db.latest_reading(),
        "online": online,
        "last_seen": live["last_seen"] or None,
        "server_time": now,
        "thresholds": db.get_setting("thresholds", DEFAULT_THRESHOLDS),
    }


@app.get("/api/history")
def history(hours: float = Query(24, gt=0, le=24 * 30), limit: int = Query(500, ge=10, le=5000)):
    return db.history(time.time() - hours * 3600, limit)


@app.get("/api/events")
def events(limit: int = Query(50, ge=1, le=500)):
    return db.recent_events(limit)


@app.get("/api/alerts")
def get_alerts():
    live = ingest.liveness()
    return alerts.derive_alerts(db.latest_reading(), live["status"], live["last_seen"],
                                time.time(), config.OFFLINE_AFTER_S)


# ---- command endpoints ----
@app.post("/api/pump")
def pump(cmd: PumpCmd):
    return _forward("/pump", cmd)


@app.post("/api/mode")
def mode(cmd: ModeCmd):
    return _forward("/mode", cmd)


@app.post("/api/thresholds")
def thresholds(cmd: ThresholdsCmd):
    resp = _forward("/thresholds", cmd)
    if resp.status_code < 300:
        db.set_setting("thresholds", cmd.model_dump())
    return resp
