import os

from dotenv import load_dotenv

load_dotenv()


def _bool(name: str, default: str = "0") -> bool:
    return os.getenv(name, default).lower() in ("1", "true", "yes")


CORS_ORIGINS = [o.strip() for o in os.getenv("CORS_ORIGINS", "http://localhost:5173").split(",") if o.strip()]
DB_PATH = os.getenv("DB_PATH", "irrigation.db")

MQTT_HOST = os.getenv("MQTT_HOST", "localhost")
MQTT_PORT = int(os.getenv("MQTT_PORT", "1883"))
MQTT_USER = os.getenv("MQTT_USER", "")
MQTT_PASS = os.getenv("MQTT_PASS", "")
MQTT_PREFIX = os.getenv("MQTT_PREFIX", "farm/field1")

DEVICE_HOST = os.getenv("DEVICE_HOST", "irrigation.local")
DEVICE_API_KEY = os.getenv("DEVICE_API_KEY", "")
DEVICE_TIMEOUT_S = float(os.getenv("DEVICE_TIMEOUT_S", "4"))

OFFLINE_AFTER_S = float(os.getenv("OFFLINE_AFTER_S", "30"))
DEMO = _bool("DEMO")
