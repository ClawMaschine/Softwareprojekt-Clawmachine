from dataclasses import dataclass
import json
import time
from typing import Optional

try:
    from python_server.configuration_loader import load_mqtt_configuration
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.device_registry import DeviceRegistry
    from python_server.clawmachine.esp_device import EspDevice
except ModuleNotFoundError:
    from configuration_loader import load_mqtt_configuration
    from mqtt import MQTTClient
    from device_registry import DeviceRegistry
    from esp_device import EspDevice


CLAWMACHINE_TOPIC_PREFIX = "clawmachine/"

CONTROL_TOPIC = "clawmachine/claw"

METADATA_UPTIME_TOPIC_WILDCARD = "clawmachine/+/metadata/uptime"
METADATA_UPTIME_TOPIC_SUFFIX = "/metadata/uptime"

INTERNAL_TOPIC_WILDCARD = "clawmachine/+/internal"
INTERNAL_TOPIC_SUFFIX = "/internal"

DEVICE_STATUS_TOPIC_WILDCARD = "clawmachine/+/status"
DEVICE_STATUS_TOPIC_SUFFIX = "/status"

# Kein eigenes "device/added"-Anmelde-Topic mehr — der Server registriert ein
# Gerät automatisch, sobald es zum ersten Mal auf einem seiner Topics (z.B.
# Uptime-Heartbeat, Status) auftaucht, und bestätigt das hierüber.
DEVICE_REGISTERED_TOPIC_SUFFIX = "/registered"

MOTOR_CONTROLLER_COMMAND_TOPIC = "clawmachine/motor_controller/motor/command"
# JSON-Settings-Update für den Motor-Controller (z.B. Beschleunigung) —
# separates Topic von den X:/Y:/Z:/claw:-Bewegungsbefehlen, siehe
# onMqttMessage() in src_claw_motor_controller/main.cpp.
MOTOR_CONTROLLER_SETTINGS_TOPIC = "clawmachine/motor_controller/settings"
MOTOR_COMMAND_PREFIXES = ("X:", "Y:", "Z:", "claw:")

PLAYER_INPUT_PANEL_TOPIC = "clawmachine/player_input/panel"
WEBINTERFACE_COMMAND_TOPIC = "clawmachine/web_interface/command"
PANEL_MOTOR_SPEED = 80


def extract_esp_name_from_topic(topic: str, suffix: str) -> Optional[str]:
    if topic.startswith(CLAWMACHINE_TOPIC_PREFIX) and topic.endswith(suffix):
        return topic[len(CLAWMACHINE_TOPIC_PREFIX) : -len(suffix)]
    return None


@dataclass
class ClawMachine:

    def __init__(self):

        mqtt_configuration = load_mqtt_configuration()
        self.control_topic = CONTROL_TOPIC
        self.device_registry = DeviceRegistry()
        self.mqtt_client = MQTTClient(
            client_id=mqtt_configuration.client_id,
            broker=mqtt_configuration.broker,
            port=mqtt_configuration.port,
            connect_timeout_seconds=mqtt_configuration.connect_timeout_seconds,
            username=mqtt_configuration.username,
            password=mqtt_configuration.password,
        )
        self.mqtt_client.connect()


        # Der Player-Input-Controller schickt beim Panel nur noch die Tasten,
        # die sich seit der letzten Nachricht geändert haben (Delta statt
        # komplettem Zustand) — deshalb hier den vollständigen Zustand über
        # mehrere Nachrichten hinweg mitführen, statt ihn pro Nachricht neu
        # zu berechnen.
        self.panel_button_state = {}

        self.setup_message_handlers()

        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        if mqtt_network_client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")
        mqtt_network_client.subscribe(self.control_topic)
        mqtt_network_client.subscribe(METADATA_UPTIME_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(INTERNAL_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(DEVICE_STATUS_TOPIC_WILDCARD)
        mqtt_network_client.subscribe(PLAYER_INPUT_PANEL_TOPIC)
        mqtt_network_client.subscribe(WEBINTERFACE_COMMAND_TOPIC)
        mqtt_network_client.on_message = self.on_message

    def ensure_device_registered(self, esp_name: str) -> Optional[EspDevice]:
        # Kein separates "device/added"-Topic mehr: taucht ein Gerätename hier
        # zum ersten Mal auf, wird er automatisch registriert und das Gerät
        # bekommt eine einmalige Bestätigung zurück. Ist es schon bekannt,
        # passiert nichts weiter — kein erneutes Registrieren/Bestätigen bei
        # jedem Heartbeat.
        device = self.device_registry.get(esp_name)
        if device is None:
            device = self.device_registry.add(esp_name)
            registered_topic = (
                f"{CLAWMACHINE_TOPIC_PREFIX}{esp_name}{DEVICE_REGISTERED_TOPIC_SUFFIX}"
            )
            self.mqtt_client.publish(registered_topic, "ok")
        return device

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

        # switch/case über die Topic-Art. `case _ if ...` prüft "passt das Topic
        # zu mir?" (per Walrus gleich mit dem extrahierten Wert), der erste
        # Treffer gewinnt, kein Fallthrough — der abschließende `case _` ist
        # der Default für alles, was zu keinem bekannten Topic passt.
        match topic:
            case _ if topic in (PLAYER_INPUT_PANEL_TOPIC, WEBINTERFACE_COMMAND_TOPIC):
                self.on_control_command(topic, payload_text)
            # 7) Steuerbefehl für die Motoren (z.B. "X:100", "claw:open") auf dem
            #    Haupt-Steuertopic — unverändert an den Motor-Controller weiterleiten
            case _ if topic == self.control_topic and payload_text.startswith(
                MOTOR_COMMAND_PREFIXES
            ):
                self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, payload_text)

            # 1) Heartbeat/Laufzeit eines Geräts (clawmachine/<name>/metadata/uptime) —
            #    taucht ein Gerätename hier zum ersten Mal auf, wird er automatisch
            #    registriert (siehe ensure_device_registered)
            case _ if (
                esp_name := extract_esp_name_from_topic(topic, METADATA_UPTIME_TOPIC_SUFFIX)
            ) is not None:
                device = self.ensure_device_registered(esp_name)
                if device is not None:
                    device.metadata.uptime_milliseconds = int(payload_text)

            # 2) Online/Offline-Status eines Geräts, meist über LWT (Last Will) gesetzt
            #    (clawmachine/<name>/status) — registriert das Gerät ebenso automatisch
            case _ if (
                esp_name := extract_esp_name_from_topic(topic, DEVICE_STATUS_TOPIC_SUFFIX)
            ) is not None:
                device = self.ensure_device_registered(esp_name)
                if device is not None:
                    device.is_online = payload_text == "online"
                    
            # Steuertopic, aber kein bekannter Befehl
            case _ if topic == self.control_topic:
                print(f"Unknown control command: {payload_text}")

            # Default: passt zu keinem der obigen Topics — ignorieren
            case _:
                pass

    def on_control_command(self, topic: str, payload_text: str):
        
        match topic:
            case _ if topic == PLAYER_INPUT_PANEL_TOPIC:
                panel_buttons = json.loads(payload_text)
                self.panel_button_state.update(panel_buttons)

                if self.panel_button_state.get("right"):
                    x_speed = -PANEL_MOTOR_SPEED
                elif self.panel_button_state.get("left"):
                    x_speed = PANEL_MOTOR_SPEED
                else:
                    x_speed = 0

                if self.panel_button_state.get("back"):
                    y_speed = PANEL_MOTOR_SPEED
                elif self.panel_button_state.get("front"):
                    y_speed = -PANEL_MOTOR_SPEED
                else:
                    y_speed = 0
                    
                if self.panel_button_state.get("up"):
                    z_speed = PANEL_MOTOR_SPEED
                elif self.panel_button_state.get("down"):
                    z_speed = -PANEL_MOTOR_SPEED
                else:
                    z_speed = 0

                self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"X:{x_speed}")
                self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"Y:{y_speed}")
                self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"Z:{z_speed}")
            # 6) Steuerbefehl vom Webinterface (z.B. "left:80", "front:-80",
            #    "claw:open") — das Webinterface rechnet die Geschwindigkeit
            #    schon selbst aus (siehe app.component.ts), der Server muss
            #    hier nur noch den Tastennamen auf die Motor-Achse mappen.
            case _ if topic == WEBINTERFACE_COMMAND_TOPIC:
                name, _, value = payload_text.strip().partition(":")

                if name == "claw":
                    self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, payload_text)
                elif name in ("left", "right"):
                    self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"X:{value}")
                elif name in ("front", "back"):
                    self.mqtt_client.publish(MOTOR_CONTROLLER_COMMAND_TOPIC, f"Y:{value}")
                elif name == "accel":
                    # Eigenes Settings-Topic statt Bewegungsbefehl — der
                    # Motor-Controller erwartet hier JSON, siehe
                    # onMqttMessage() in src_claw_motor_controller/main.cpp.
                    try:
                        acceleration = float(value)
                    except ValueError:
                        print(f"Invalid acceleration value: {value}")
                        return
                    self.mqtt_client.publish(
                        MOTOR_CONTROLLER_SETTINGS_TOPIC,
                        json.dumps({"accelerationPercentPerSecond": acceleration}),
                    )
                else:
                    print(f"Unknown webinterface command: {payload_text}")


    def main_loop(self):
        while True:
            time.sleep(1)


