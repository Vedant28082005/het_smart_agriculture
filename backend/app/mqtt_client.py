"""Background paho-mqtt subscriber feeding ingest."""
import paho.mqtt.client as mqtt

from . import config, ingest


def start() -> mqtt.Client:
    prefix = config.MQTT_PREFIX
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    if config.MQTT_USER:
        client.username_pw_set(config.MQTT_USER, config.MQTT_PASS)

    def on_connect(c, userdata, flags, reason_code, properties=None):
        c.subscribe([(f"{prefix}/sensors", 0), (f"{prefix}/events", 0), (f"{prefix}/status", 0)])

    def on_message(c, userdata, msg):
        topic = msg.topic.rsplit("/", 1)[-1]
        if topic == "sensors":
            ingest.handle_sensors(msg.payload)
        elif topic == "events":
            ingest.handle_event(msg.payload)
        elif topic == "status":
            ingest.handle_status(msg.payload)

    client.on_connect = on_connect
    client.on_message = on_message
    client.reconnect_delay_set(1, 30)
    client.connect_async(config.MQTT_HOST, config.MQTT_PORT)  # does not block startup if the broker is down
    client.loop_start()
    return client
