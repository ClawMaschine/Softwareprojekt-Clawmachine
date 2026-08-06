from dataclasses import dataclass
import time
from typing import Optional

try:
    from python_server.configuration_loader import load_mqtt_configuration
    from python_server.esp.mqtt_esp_controller import MQTTEspController
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.device_registry import DeviceRegistry
except ModuleNotFoundError:
    from configuration_loader import load_mqtt_configuration
    from esp.mqtt_esp_controller import MQTTEspController
    from mqtt import MQTTClient
    from device_registry import DeviceRegistry

KNOWN_ESP_NAMES = ["motor_controller", "control_panel"]

CLAWMACHINE_TOPIC_PREFIX = "clawmachine/"

METADATA_UPTIME_TOPIC_WILDCARD = "clawmachine/+/metadata/uptime"
METADATA_UPTIME_TOPIC_SUFFIX = "/metadata/uptime"

INTERNAL_TOPIC_WILDCARD = "clawmachine/+/internal"
INTERNAL_TOPIC_SUFFIX = "/internal"

DEVICE_STATUS_TOPIC_WILDCARD = "clawmachine/+/status"
DEVICE_STATUS_TOPIC_SUFFIX = "/status"

MOTOR_CONTROLLER_COMMAND_TOPIC = "clawmachine/motor_controller/motor/command"
MOTOR_COMMAND_PREFIXES = ("X:", "Y:", "Z:", "claw:")


def extract_esp_name_from_topic(topic: str, suffix: str) -> Optional[str]:
    if topic.startswith(CLAWMACHINE_TOPIC_PREFIX) and topic.endswith(suffix):
        return topic[len(CLAWMACHINE_TOPIC_PREFIX) : -len(suffix)]
    return None


@dataclass
class ClawMachine:

    def __init__(self):

        mqtt_configuration = load_mqtt_configuration()
        self.control_topic = mqtt_configuration.topic
        self.device_registry = DeviceRegistry(
            known_esp_names=KNOWN_ESP_NAMES,
            device_added_topic=mqtt_configuration.device_added_topic,
        )
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker="mqtt-broker",
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
            username=mqtt_configuration.username,
            password=mqtt_configuration.password,
        )
        self.mqtt_client.connect()
        self.esp_controller = MQTTEspController(self.mqtt_client)

        self.setup_message_handlers()
        self.mqtt_client.publish(self.control_topic, "open")

        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        if mqtt_network_client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")
        mqtt_network_client.subscribe(self.control_topic)
        mqtt_network_client.subscribe(self.device_registry.device_added_topic)
        mqtt_network_client.subscribe(METADATA_UPTIME_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(INTERNAL_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(DEVICE_STATUS_TOPIC_WILDCARD)
        mqtt_network_client.on_message = self.on_message

    def on_message(self, _client, _userdata, message):
        # Callback von paho-mqtt für JEDE Nachricht auf einem abonnierten Topic
        # (siehe setup_message_handlers). topic/payload kommen als bytes an,
        # daher hier einmalig in str dekodieren.
        topic = (
            message.topic
            if isinstance(message.topic, str)
            else message.topic.decode("utf-8", errors="replace")
        )
        payload_text = message.payload.decode("utf-8", errors="replace").strip()
        print(f"Received message on topic '{topic}': {payload_text}")

        device = self.device_registry.get_by_topic(topic)
        # switch/case über die Topic-Art. `case _ if ...` prüft "passt das Topic
        # zu mir?" (per Walrus gleich mit dem extrahierten Wert), der erste
        # Treffer gewinnt, kein Fallthrough — der abschließende `case _` ist
        # der Default für alles, was zu keinem bekannten Topic passt.
        match topic:
            # 1) Neues/erneut verbundenes ESP32-Gerät meldet sich (clawmachine/device/added)
            case _ if (
                added_device_name := self.device_registry.extract_device_name(
                    topic, payload_text
                )
            ) is not None:
                self.device_registry.register(added_device_name)

            # 2) Heartbeat/Laufzeit eines Geräts (clawmachine/<name>/metadata/uptime)
            case _ if (
                esp_name := extract_esp_name_from_topic(topic, METADATA_UPTIME_TOPIC_SUFFIX)
            ) is not None:
                device = self.device_registry.get(esp_name)
                if device is not None:
                    device.metadata.uptime_milliseconds = int(payload_text)

            # 3) Geräte-interne Nachricht, wird an das jeweilige EspDevice weitergereicht
            #    (clawmachine/<name>/internal)
            case _ if (
                esp_name := extract_esp_name_from_topic(topic, INTERNAL_TOPIC_SUFFIX)
            ) is not None:
                device = self.device_registry.get(esp_name)
                if device is not None:
                    device.on_message(topic, payload_text)

            # 4) Online/Offline-Status eines Geräts, meist über LWT (Last Will) gesetzt
            #    (clawmachine/<name>/status)
            case _ if (
                esp_name := extract_esp_name_from_topic(topic, DEVICE_STATUS_TOPIC_SUFFIX)
            ) is not None:
                device = self.device_registry.get(esp_name)
                if device is not None:
                    device.is_online = payload_text == "online"

            # 5) Steuerbefehl für die Motoren (z.B. "X:100", "claw:open") auf dem
            #    Haupt-Steuertopic — unverändert an den Motor-Controller weiterleiten
            case _ if topic == self.control_topic and payload_text.startswith(
                MOTOR_COMMAND_PREFIXES
            ):
                self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, payload_text)

            # Steuertopic, aber kein bekannter Befehl
            case _ if topic == self.control_topic:
                print(f"Unknown control command: {payload_text}")
            # Default: passt zu keinem der obigen Topics — ignorieren
            case _:
                pass

    def main_loop(self):
        while True:
            time.sleep(1)

    def move_to(self, x, y):
        self.position = (x, y)
        print(f"Moved to position: {self.position}")

    def open_claw(self):
        self.is_claw_open = True
        print("Claw opened")

    def close_claw(self):
        self.is_claw_open = False
        print("Claw closed")
