import time
from typing import Optional

try:
    from python_server.configuration_loader import load_mqtt_configuration
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.device_registry import DeviceRegistry
    from python_server.clawmachine.esp import MotorControllerEsp
    from python_server.clawmachine.esp_descriptor import EspDescriptor, HardwareDescriptor
    from python_server.clawmachine.input_event import InputEvent
except ModuleNotFoundError:
    from configuration_loader import load_mqtt_configuration
    from mqtt import MQTTClient
    from clawmachine.device_registry import DeviceRegistry
    from clawmachine.esp import MotorControllerEsp
    from clawmachine.esp_descriptor import EspDescriptor, HardwareDescriptor
    from clawmachine.input_event import InputEvent

# Vorab bekannte ESPs, die schon vor dem ersten Discovery-Event existieren.
KNOWN_ESP_DESCRIPTORS = [
    EspDescriptor(
        name="motor_controller",
        type="motor_controller",
        hardware=[HardwareDescriptor(name="axis_1", type="motor")],
    ),
    EspDescriptor(name="control_panel", type="control_panel"),
]

# Zentrales Mapping: Spieler-Eingabe → Motorbefehl. Die ClawMachine vermittelt
# damit lose zwischen Input- und Output-ESPs (keine direkte Kopplung).
PLAYER_INPUT_TO_MOTOR_COMMAND = {
    "left": ("move_x", "-1"),
    "right": ("move_x", "+1"),
    "up": ("move_y", "+1"),
    "down": ("move_y", "-1"),
    "open": ("claw", "open"),
    "close": ("claw", "close"),
}

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


class ClawMachine:
    """Zentrale Steuer- und Vermittlungsstelle der Claw Machine.

    Hält alle entdeckten ESP-Objekte (über die DeviceRegistry) und vermittelt
    zwischen ihnen: meldet ein PlayerInputEsp eine Eingabe, entscheidet die
    ClawMachine als Zentrale, welcher MotorControllerEsp angesteuert wird.
    """

    def __init__(self):
        self.is_claw_open = False

        mqtt_configuration = load_mqtt_configuration()
        self.control_topic = mqtt_configuration.topic
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
            username=mqtt_configuration.username,
            password=mqtt_configuration.password,
        )
        self.mqtt_client.connect()
        self.device_registry = DeviceRegistry(
            mqtt_client=self.mqtt_client,
            device_added_topic=mqtt_configuration.device_added_topic,
            on_esp_registered=self.on_esp_registered,
            known_esp_descriptors=KNOWN_ESP_DESCRIPTORS,
        )

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

    def on_esp_registered(self, esp):
        """Verdrahtet einen neu registrierten ESP mit der Zentrale.

        Eingaben fließen damit über `handle_input_event` zur ClawMachine,
        statt Input- und Output-Objekt direkt zu koppeln.
        """
        esp.set_input_listener(self.handle_input_event)

    def handle_input_event(self, event: InputEvent):
        """Zentrale Reaktion auf eine Spieler-Eingabe (Vermittlung an Motoren)."""
        print(
            f"Input from {event.source_esp_name}/{event.hardware_name}: "
            f"{event.action} ({event.value})"
        )
        motor_command = PLAYER_INPUT_TO_MOTOR_COMMAND.get(event.action)
        if motor_command is None:
            print(f"No motor mapping for input action: {event.action}")
            return

        action, value = motor_command
        motor_controllers = self.device_registry.esps_of_type(MotorControllerEsp)
        if not motor_controllers:
            print("No motor controller registered – cannot route input")
            return
        for motor_controller in motor_controllers:
            motor_controller.drive(action, value)

    def on_message(self, _client, _userdata, message):
        topic = (
            message.topic
            if isinstance(message.topic, str)
            else message.topic.decode("utf-8", errors="replace")
        )
        payload_text = message.payload.decode("utf-8", errors="replace").strip()
        print(f"Received message on topic '{topic}': {payload_text}")

        registered_esp = self.device_registry.register_from_payload(topic, payload_text)
        if registered_esp is not None:
            return

        esp_name = extract_esp_name_from_topic(topic, METADATA_UPTIME_TOPIC_SUFFIX)
        if esp_name is not None:
            esp = self.device_registry.get(esp_name)
            if esp is not None:
                esp.metadata.uptime_milliseconds = int(payload_text)
            return

        esp_name = extract_esp_name_from_topic(topic, INTERNAL_TOPIC_SUFFIX)
        if esp_name is not None:
            esp = self.device_registry.get(esp_name)
            if esp is not None:
                esp.on_message(topic, payload_text)
            return

        esp_name = extract_esp_name_from_topic(topic, DEVICE_STATUS_TOPIC_SUFFIX)
        if esp_name is not None:
            esp = self.device_registry.get(esp_name)
            if esp is not None:
                esp.is_online = payload_text == "online"
            return

        if topic != self.control_topic:
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
