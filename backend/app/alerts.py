"""Active alerts, derived from the newest reading and device liveness."""


def device_online(status: str, last_seen: float, now: float, offline_after: float) -> bool:
    if status == "offline" or last_seen <= 0:
        return False
    return (now - last_seen) <= offline_after


def derive_alerts(latest: dict | None, status: str, last_seen: float, now: float,
                  offline_after: float) -> list[dict]:
    alerts = []
    if not device_online(status, last_seen, now, offline_after):
        alerts.append({"code": "DEVICE_OFFLINE", "severity": "critical",
                       "message": "Device is offline (no data or broker reports offline)."})
        return alerts  # everything else would be stale data
    if latest is None:
        return alerts
    if not latest["tank_ok"]:
        alerts.append({"code": "TANK_EMPTY", "severity": "critical",
                       "message": "Water tank is empty. Pump is locked out."})
    if not latest["sensor_ok"] or latest.get("reason") == "LOCKOUT_SENSOR_FAIL":
        alerts.append({"code": "SENSOR_FAILURE", "severity": "serious",
                       "message": "A sensor is failing. Auto irrigation is paused."})
    if latest.get("reason") == "LOCKOUT_MAX_RUNTIME":
        alerts.append({"code": "MAX_RUNTIME", "severity": "warning",
                       "message": "Pump hit its max run time and is cooling down."})
    return alerts
