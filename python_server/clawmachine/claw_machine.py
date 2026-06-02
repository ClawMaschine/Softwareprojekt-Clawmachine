from dataclasses import dataclass
import time
<<<<<<< HEAD
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


def extract_esp_name_from_topic(topic: str, suffix: str) -> Optional[str]:
    if topic.startswith(CLAWMACHINE_TOPIC_PREFIX) and topic.endswith(suffix):
        return topic[len(CLAWMACHINE_TOPIC_PREFIX) : -len(suffix)]
    return None
=======

from python_server.configuration_loader import load_mqtt_configuration
from python_server.esp.mqtt_esp_controller import MQTTEspController
from python_server.mqtt import MQTTClient
>>>>>>> 2682625 (clawmachine moved to own file)


@dataclass
class ClawMachine:

    def __init__(self):
        self.is_claw_open = False
<<<<<<< HEAD

        mqtt_configuration = load_mqtt_configuration()
        self.control_topic = mqtt_configuration.topic
        self.device_registry = DeviceRegistry(
            known_esp_names=KNOWN_ESP_NAMES,
            device_added_topic=mqtt_configuration.device_added_topic,
        )
=======
        self.connected_devices_by_name = {}
        mqtt_configuration = load_mqtt_configuration()
        self.device_added_topic = mqtt_configuration.device_added_topic
>>>>>>> 2682625 (clawmachine moved to own file)
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
<<<<<<< HEAD
            username=mqtt_configuration.username,
            password=mqtt_configuration.password,
=======
>>>>>>> 2682625 (clawmachine moved to own file)
        )
        self.mqtt_client.connect()
        self.esp_controller = MQTTEspController(self.mqtt_client)

        self.setup_message_handlers()
<<<<<<< HEAD
        self.mqtt_client.publish(self.control_topic, "open")
=======
        self.mqtt_client.publish(mqtt_configuration.topic, "open")
>>>>>>> 2682625 (clawmachine moved to own file)

        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        if mqtt_network_client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")
<<<<<<< HEAD
        mqtt_network_client.subscribe(self.control_topic)
        mqtt_network_client.subscribe(self.device_registry.device_added_topic)
        mqtt_network_client.subscribe(METADATA_UPTIME_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(INTERNAL_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(DEVICE_STATUS_TOPIC_WILDCARD)
        mqtt_network_client.on_message = self.on_message

    def on_message(self, _client, _userdata, message):
=======
        self.control_topic = load_mqtt_configuration().topic
        mqtt_network_client.subscribe(self.control_topic)
        mqtt_network_client.subscribe(self.device_added_topic)
        mqtt_network_client.on_message = self.on_message
        self.esp_controller.get_connected_esps()

    def register_device(self, device_name):
        cleaned_device_name = device_name.strip()
        if not cleaned_device_name:
            return None

        existing_device = self.connected_devices_by_name.get(cleaned_device_name)
        if existing_device is not None:
            return existing_device

        new_device = {
            "name": cleaned_device_name,
            "added_at_unix_seconds": time.time(),
        }
        self.connected_devices_by_name[cleaned_device_name] = new_device
        print(f"Device added: {cleaned_device_name}")
        print(f"Known devices: {sorted(self.connected_devices_by_name.keys())}")
        return new_device

    def extract_device_name_from_message(self, topic, payload_text):
        if topic == self.device_added_topic:
            print(f"Extracting device name from topic: {topic}")

            return payload_text

        topic_prefix = "clawmachine/device/"
        topic_suffix = "/added"
        if topic.startswith(topic_prefix) and topic.endswith(topic_suffix):
            return topic[len(topic_prefix) : -len(topic_suffix)]

        return None

    def on_message(self, _client, _userdata, message):
        print(f"Received MQTT message on topic: {message.topic}")
        print(f"Message payload: {message.payload}")
>>>>>>> 2682625 (clawmachine moved to own file)
        topic = (
            message.topic
            if isinstance(message.topic, str)
            else message.topic.decode("utf-8", errors="replace")
        )
        payload_text = message.payload.decode("utf-8", errors="replace").strip()

<<<<<<< HEAD
        added_device_name = self.device_registry.extract_device_name(
            topic, payload_text
        )
        if added_device_name is not None:
            self.device_registry.register(added_device_name)
            return

        esp_name = extract_esp_name_from_topic(topic, METADATA_UPTIME_TOPIC_SUFFIX)
        if esp_name is not None:
            device = self.device_registry.get(esp_name)
            if device is not None:
                device.metadata.uptime_milliseconds = int(payload_text)
            return

        esp_name = extract_esp_name_from_topic(topic, INTERNAL_TOPIC_SUFFIX)
        if esp_name is not None:
            device = self.device_registry.get(esp_name)
            if device is not None:
                device.on_message(topic, payload_text)
            return

        esp_name = extract_esp_name_from_topic(topic, DEVICE_STATUS_TOPIC_SUFFIX)
        if esp_name is not None:
            device = self.device_registry.get(esp_name)
            if device is not None:
                device.is_online = payload_text == "online"
=======
        added_device_name = self.extract_device_name_from_message(topic, payload_text)
        if added_device_name is not None:
            self.register_device(added_device_name)
>>>>>>> 2682625 (clawmachine moved to own file)
            return

        if topic != self.control_topic:
            return

<<<<<<< HEAD
=======
        if payload_text == "open":
            self.open_claw()
            return

        if payload_text == "close":
            self.close_claw()
            return

>>>>>>> 2682625 (clawmachine moved to own file)
        print(f"Unknown control command: {payload_text}")

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
