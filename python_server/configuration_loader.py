from configparser import ConfigParser
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass(frozen=True)
class MqttConfiguration:
    broker: str
    port: int
    topic: str
    device_added_topic: str
    client_id: str
    connect_timeout_seconds: float
    username: Optional[str]
    password: Optional[str]


def load_mqtt_configuration() -> MqttConfiguration:
    config_parser = ConfigParser()
    configuration_file_path = Path(__file__).with_name("config.ini")
    config_parser.read(configuration_file_path)

    broker = config_parser.get("mqtt", "broker", fallback="localhost")
    port = config_parser.getint("mqtt", "port", fallback=1883)
    topic = config_parser.get("mqtt", "topic", fallback="clawmachine/claw")
    device_added_topic = config_parser.get(
        "mqtt", "device_added_topic", fallback="clawmachine/device/added"
    )
    client_id = config_parser.get("mqtt", "client_id", fallback="server")
    connect_timeout_seconds = config_parser.getfloat(
        "mqtt", "connect_timeout_seconds", fallback=5.0
    )
    username = config_parser.get("mqtt", "username", fallback=None)
    password = config_parser.get("mqtt", "password", fallback=None)

    return MqttConfiguration(
        broker=broker,
        port=port,
        topic=topic,
        device_added_topic=device_added_topic,
        client_id=client_id,
        connect_timeout_seconds=connect_timeout_seconds,
        username=username,
        password=password,
    )
