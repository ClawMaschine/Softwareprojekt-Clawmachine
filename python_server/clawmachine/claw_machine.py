from dataclasses import dataclass
import time

from python_server.configuration_loader import load_mqtt_configuration
from python_server.esp.mqtt_esp_controller import MQTTEspController
from python_server.mqtt import MQTTClient


@dataclass
class ClawMachine:

    def __init__(self):
        self.is_claw_open = False
        self.connected_devices_by_name = {}
        mqtt_configuration = load_mqtt_configuration()
        self.device_added_topic = mqtt_configuration.device_added_topic
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
        )
        self.mqtt_client.connect()
        self.esp_controller = MQTTEspController(self.mqtt_client)

        self.setup_message_handlers()
        self.mqtt_client.publish(mqtt_configuration.topic, "open")

        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        if mqtt_network_client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")
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
        topic = (
            message.topic
            if isinstance(message.topic, str)
            else message.topic.decode("utf-8", errors="replace")
        )
        payload_text = message.payload.decode("utf-8", errors="replace").strip()

        added_device_name = self.extract_device_name_from_message(topic, payload_text)
        if added_device_name is not None:
            self.register_device(added_device_name)
            return

        if topic != self.control_topic:
            return

        if payload_text == "open":
            self.open_claw()
            return

        if payload_text == "close":
            self.close_claw()
            return

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
