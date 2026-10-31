"""Forwards validated commands to the ESP32's HTTP server."""
import httpx

from . import config


class DeviceUnreachable(Exception):
    pass


def request(method: str, path: str, body: dict | None = None) -> tuple[int, dict]:
    """Returns (status_code, json_body) from the device. Raises DeviceUnreachable on network failure."""
    if config.DEMO:
        from . import demo
        return demo.handle(method, path, body)
    url = f"http://{config.DEVICE_HOST}{path}"
    try:
        r = httpx.request(method, url, json=body, headers={"X-API-Key": config.DEVICE_API_KEY},
                          timeout=config.DEVICE_TIMEOUT_S)
    except httpx.HTTPError as e:
        raise DeviceUnreachable(str(e)) from e
    try:
        data = r.json()
    except ValueError:
        data = {"error": r.text[:200]}
    return r.status_code, data
