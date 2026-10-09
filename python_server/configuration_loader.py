from configparser import ConfigParser
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class MqttConfiguration:
    broker: str
    port: int
    username: str
    password: str
    client_id: str
    connect_timeout_seconds: float


def load_mqtt_configuration() -> MqttConfiguration:
    config_parser = ConfigParser()
    project_root = Path(__file__).parent.parent
    # config.local.ini überschreibt config.ini — lokal anlegen für abweichende Werte
    config_parser.read([project_root / "config.ini", project_root / "config.local.ini"])

    broker = config_parser.get("mqtt", "broker", fallback="localhost")
    port = config_parser.getint("mqtt", "port", fallback=1883)
    client_id = config_parser.get("mqtt", "client_id", fallback="server")
    connect_timeout_seconds = config_parser.getfloat(
        "mqtt", "connect_timeout_seconds", fallback=5.0
    )
    username = config_parser.get("mqtt", "username", fallback=None)
    password = config_parser.get("mqtt", "password", fallback=None)

    return MqttConfiguration(
        broker=broker,
        port=port,
        username=username,
        password=password,
        client_id=client_id,
        connect_timeout_seconds=connect_timeout_seconds,
    )


@dataclass(frozen=True)
class CameraConfiguration:
    device_index: int
    jpeg_quality: int
    frame_rate: float
    http_port: int


def load_camera_configuration() -> CameraConfiguration:
    config_parser = ConfigParser()
    project_root = Path(__file__).parent.parent
    config_parser.read([project_root / "config.ini", project_root / "config.local.ini"])

    device_index = config_parser.getint("camera", "device_index", fallback=0)
    jpeg_quality = config_parser.getint("camera", "jpeg_quality", fallback=80)
    frame_rate = config_parser.getfloat("camera", "frame_rate", fallback=15.0)
    http_port = config_parser.getint("camera", "http_port", fallback=8000)

    return CameraConfiguration(
        device_index=device_index,
        jpeg_quality=jpeg_quality,
        frame_rate=frame_rate,
        http_port=http_port,
    )
