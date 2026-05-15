from time import sleep
import time

try:
    from .config import load_mqtt_configuration
    from .mqtt import MQTTClient
except ImportError:
    from config import load_mqtt_configuration
    from mqtt import MQTTClient


class ClawMachine:

    def __init__(self):
        self.position = (0, 0)
        self.is_claw_open = False
        self.connected_devices_by_name = {}
        self.control_topic = "clawmachine/joycon"
        mqtt_configuration = load_mqtt_configuration()
        self.device_added_topic = mqtt_configuration.device_added_topic
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
        )
        self.mqtt_client.connect()
        sleep(0.2)
        self.mqtt_client.publish(mqtt_configuration.topic, "open")
        self.setup_message_handlers()
        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        mqtt_network_client.subscribe(self.control_topic)
        mqtt_network_client.subscribe(self.device_added_topic)
        mqtt_network_client.on_message = self.on_message

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
            return payload_text

        topic_prefix = "clawmachine/device/"
        topic_suffix = "/added"
        if topic.startswith(topic_prefix) and topic.endswith(topic_suffix):
            return topic[len(topic_prefix) : -len(topic_suffix)]

        return None

    def on_message(self, _client, _userdata, message):
        topic = message.topic if isinstance(message.topic, str) else message.topic.decode("utf-8", errors="replace")
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
            sleep(1)

    def move_to(self, x, y):
        self.position = (x, y)
        print(f"Moved to position: {self.position}")

    def open_claw(self):
        self.is_claw_open = True
        print("Claw opened")

    def close_claw(self):
        self.is_claw_open = False
        print("Claw closed")


def run():
    ClawMachine()


if __name__ == "__main__":
    run()
