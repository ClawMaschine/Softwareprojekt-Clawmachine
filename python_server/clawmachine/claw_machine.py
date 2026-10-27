from dataclasses import dataclass
import json
import time
from typing import Optional

try:
    from python_server.configuration_loader import (
        load_motor_speed_configuration,
        load_mqtt_configuration,
    )
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.device_registry import DeviceRegistry
    from python_server.clawmachine.esp_device import EspDevice
    from python_server.clawmachine.moving import Moving
    from python_server.clawmachine.web_interface_presence import WebInterfacePresence
    from python_server.clawmachine.input_source_control import InputSourceControl
    from python_server.clawmachine.topics import (
        CONTROL_TOPIC,
        ENDSTOP_TOPIC,
        MOTOR_CONTROLLER_COMMAND_TOPIC,
        MOTOR_CONTROLLER_SETTINGS_TOPIC,
        PLAYER_INPUT_JOYCON_TOPIC,
        PLAYER_INPUT_PANEL_TOPIC,
        WEB_INTERFACE_COMMAND_TOPIC,
        WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC,
        WEB_INTERFACE_DEVICE_LIST_TOPIC,
        WEB_INTERFACE_ENDSTOP_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_TOPIC,
        WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC,
        WEB_INTERFACE_SPEED_SETTINGS_TOPIC,
        device_topic,
        parse_device_name,
    )
except ModuleNotFoundError:
    from configuration_loader import (
        load_motor_speed_configuration,
        load_mqtt_configuration,
    )
    from mqtt import MQTTClient
    from device_registry import DeviceRegistry
    from esp_device import EspDevice
    from moving import Moving
    from web_interface_presence import WebInterfacePresence
    from input_source_control import InputSourceControl
    from topics import (
        CONTROL_TOPIC,
        ENDSTOP_TOPIC,
        MOTOR_CONTROLLER_COMMAND_TOPIC,
        MOTOR_CONTROLLER_SETTINGS_TOPIC,
        PLAYER_INPUT_JOYCON_TOPIC,
        PLAYER_INPUT_PANEL_TOPIC,
        WEB_INTERFACE_COMMAND_TOPIC,
        WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC,
        WEB_INTERFACE_DEVICE_LIST_TOPIC,
        WEB_INTERFACE_ENDSTOP_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC,
        WEB_INTERFACE_INPUT_SOURCES_TOPIC,
        WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC,
        WEB_INTERFACE_SPEED_SETTINGS_TOPIC,
        device_topic,
        parse_device_name,
    )


MOTOR_COMMAND_PREFIXES = ("X:", "Y:", "Z:", "claw:")

# Der Joy-Con (src_claw_switch2_scanner) sendet dieselben Feldnamen wie das
# Control-Panel, deshalb dieselbe Auswertung — nur ohne grab/release. Das Panel
# sendet 0/1 pro Taste, der Joy-Con 0-100 (Auslenkung/Druck in Prozent).
PLAYER_INPUT_TOPICS = (PLAYER_INPUT_PANEL_TOPIC, PLAYER_INPUT_JOYCON_TOPIC)
# Welcher InputSourceControl-Name zu welchem Eingabe-Topic gehört — nur diese
# beiden sind über das Webinterface sperrbar (siehe WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC).
INPUT_SOURCE_NAME_BY_TOPIC = {
    PLAYER_INPUT_PANEL_TOPIC: "panel",
    PLAYER_INPUT_JOYCON_TOPIC: "joycon",
}
# Präfix, unter dem sich jeder Webinterface-Browser-Tab per LWT an-/abmeldet —
# gefolgt von einer pro Tab zufälligen vierstelligen ID (siehe webInterfaceId
# in frontend/src/app/services/mqtt.service.ts), damit mehrere gleichzeitig
# offene Tabs sich nicht denselben Status-Topic teilen. Kein ESP32 — wird
# deshalb bewusst nicht in der DeviceRegistry registriert, siehe
# WebInterfacePresence.
WEB_INTERFACE_DEVICE_PREFIX = "claw_web_interface_"
# Client-ID des Motor-Controller-ESP (CLAW_MOTOR_CONTROLLER_CLIENT_ID) — sobald
# er sich (neu) meldet, bekommt er die Klauen-Geschwindigkeit nachgeschickt.
MOTOR_CONTROLLER_DEVICE_NAME = "claw_motor_controller"


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
        motor_speed_configuration = load_motor_speed_configuration()
        self.claw_speed_degrees_per_second = motor_speed_configuration.claw_degrees_per_second
        self.move_controller = Moving(self.mqtt_client, motor_speed_configuration)
        self.web_interface_presence = WebInterfacePresence()
        self.input_source_control = InputSourceControl(
            INPUT_SOURCE_NAME_BY_TOPIC.values()
        )

        # Der Player-Input-Controller schickt beim Panel nur noch die Tasten,
        # die sich seit der letzten Nachricht geändert haben (Delta statt
        # komplettem Zustand) — deshalb hier den vollständigen Zustand über
        # mehrere Nachrichten hinweg mitführen, statt ihn pro Nachricht neu
        # zu berechnen.
        self.panel_button_state = {}

        self.setup_message_handlers()

        # Subscriptions muessen vorher stehen, sonst koennten die
        # Endstop-Updates, die die Sequenz voranschalten, nie ankommen.
        self.publish_claw_speed()
        self.move_controller.start_homing_sequence()

        self.main_loop_started_at = time.time()
        self.main_loop()

    def setup_message_handlers(self):
        mqtt_network_client = self.mqtt_client.client
        if mqtt_network_client is None:
            raise RuntimeError("MQTT client is not connected. Call connect() first.")
        self.mqtt_client.subscribe(self.control_topic)
        self.mqtt_client.subscribe(device_topic("+", "metadata", "uptime"))
        self.mqtt_client.subscribe(device_topic("+", "status"))
        self.mqtt_client.subscribe(WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC)
        self.mqtt_client.subscribe(PLAYER_INPUT_PANEL_TOPIC)
        self.mqtt_client.subscribe(PLAYER_INPUT_JOYCON_TOPIC)
        self.mqtt_client.subscribe(WEB_INTERFACE_COMMAND_TOPIC)
        self.mqtt_client.subscribe(WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC)
        self.mqtt_client.subscribe(WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC)
        self.mqtt_client.subscribe(WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC)
        self.mqtt_client.subscribe(ENDSTOP_TOPIC)
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
            registered_topic = device_topic(esp_name, "registered")
            self.mqtt_client.publish(registered_topic, "ok")
            if esp_name == MOTOR_CONTROLLER_DEVICE_NAME:
                self.publish_claw_speed()
        return device

    def publish_claw_speed(self):
        # Die Klauen-Geschwindigkeit liegt als Setting auf dem Motor-Controller
        # (Grad pro Sekunde) und geht verloren, wenn dieser neu startet — deshalb
        # beim Serverstart und bei jeder (Neu-)Registrierung erneut senden.
        self.mqtt_client.publish(
            MOTOR_CONTROLLER_SETTINGS_TOPIC,
            json.dumps({"clawSpeedDegreesPerSecond": self.claw_speed_degrees_per_second}),
        )

    def publish_speed_settings(self):
        # Antwort auf WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC — wird zusaetzlich nach
        # jeder Aenderung ueber die Befehle xy_speed:/z_speed:/claw_speed: erneut gepusht.
        self.mqtt_client.publish(
            WEB_INTERFACE_SPEED_SETTINGS_TOPIC,
            json.dumps(
                {
                    "xyPercent": self.move_controller.max_speed_percent("X"),
                    "zPercent": self.move_controller.max_speed_percent("Z"),
                    "clawDegreesPerSecond": self.claw_speed_degrees_per_second,
                }
            ),
        )

    def publish_device_list(self):
        # Antwort auf WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC — wird nur auf Anfrage
        # geschickt, nicht automatisch bei jeder Änderung der Registry.
        devices = [
            device.to_dict() for device in self.device_registry.devices_by_name.values()
        ]
        self.mqtt_client.publish(WEB_INTERFACE_DEVICE_LIST_TOPIC, json.dumps(devices))

    def publish_input_sources(self):
        # Antwort auf WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC — wird zusätzlich nach
        # jeder erfolgreichen Änderung über WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC erneut
        # geschickt, damit alle offenen Webinterfaces sofort den neuen
        # Stand sehen, ohne selbst erneut anfragen zu müssen.
        self.mqtt_client.publish(
            WEB_INTERFACE_INPUT_SOURCES_TOPIC,
            json.dumps(self.input_source_control.to_dict()),
        )

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
        # zu mir?", der erste Treffer gewinnt, kein Fallthrough — der
        # abschließende `case _` ist der Default für alles, was zu keinem
        # bekannten Topic passt.
        match topic:
            case _ if topic == ENDSTOP_TOPIC:
                # Rohstatus vom Endstop-Board, z.B. {"x1":0,"x2":0,"y1":0,"y2":0,"z1":0}.
                # Der Motor-Controller-ESP bekommt das nicht direkt — Homing
                # faehrt ausschliesslich der Server (siehe Moving.home_axis()/
                # update_endstop_status() in moving.py).
                try:
                    endstop_state = json.loads(payload_text)
                except json.JSONDecodeError:
                    print(f"Invalid endstop state received: {payload_text}")
                    return
                self.move_controller.update_endstop_status(endstop_state)
                self.mqtt_client.publish(WEB_INTERFACE_ENDSTOP_TOPIC, payload_text)

            case _ if (
                topic in PLAYER_INPUT_TOPICS or topic == WEB_INTERFACE_COMMAND_TOPIC
            ):
                self.on_control_command(topic, payload_text)

            # Webinterface fragt aktiv nach der aktuellen Geräteliste
            case _ if topic == WEB_INTERFACE_DEVICE_LIST_REQUEST_TOPIC:
                self.publish_device_list()

            # Webinterface fragt aktiv nach dem aktuellen Sperr-Zustand der
            # Eingabequellen (Panel/Joy-Con)
            case _ if topic == WEB_INTERFACE_INPUT_SOURCES_REQUEST_TOPIC:
                self.publish_input_sources()

            case _ if topic == WEB_INTERFACE_SPEED_SETTINGS_REQUEST_TOPIC:
                self.publish_speed_settings()

            # Webinterface sperrt/entsperrt eine Eingabequelle, z.B. "panel:off"
            case _ if topic == WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC:
                source_name, _, value = payload_text.strip().partition(":")
                if value not in ("on", "off"):
                    print(f"Invalid input source command: {payload_text}")
                    return
                if not self.input_source_control.set_allowed(
                    source_name, value == "on"
                ):
                    print(f"Unknown input source: {source_name}")
                    return
                self.publish_input_sources()
            # 7) Steuerbefehl für die Motoren (z.B. "X:100", "claw:open") auf dem
            #    Haupt-Steuertopic — unverändert an den Motor-Controller weiterleiten
            case _ if topic == self.control_topic and payload_text.startswith(
                MOTOR_COMMAND_PREFIXES
            ):
                axis, _, value = payload_text.partition(":")
                if axis == "claw":
                    self.mqtt_client.publish(
                        MOTOR_CONTROLLER_COMMAND_TOPIC, payload_text
                    )
                else:
                    try:
                        speed = int(value)
                    except ValueError:
                        print(f"Invalid motor command: {payload_text}")
                        return
                    self.move_controller.move_axis(axis, speed)
            # 1) Heartbeat/Laufzeit eines Geräts (clawmachine/<name>/metadata/uptime) —
            #    taucht ein Gerätename hier zum ersten Mal auf, wird er automatisch
            #    registriert (siehe ensure_device_registered)
            case _ if parse_device_name(topic, "metadata", "uptime") is not None:
                esp_name = parse_device_name(topic, "metadata", "uptime")
                device = self.ensure_device_registered(esp_name)
                if device is not None:
                    device.metadata.uptime_milliseconds = int(payload_text)

            # 2) Online/Offline-Status eines Geräts, meist über LWT (Last Will) gesetzt
            #    (clawmachine/<name>/status) — registriert das Gerät ebenso automatisch.
            #    Ausnahme: der Webinterface-Browser-Client ist keine Hardware und
            #    soll nicht in der Geräteliste auftauchen (siehe WebInterfacePresence).
            case _ if parse_device_name(topic, "status") is not None:
                esp_name = parse_device_name(topic, "status")
                if esp_name.startswith(WEB_INTERFACE_DEVICE_PREFIX):
                    web_interface_id = esp_name[len(WEB_INTERFACE_DEVICE_PREFIX) :]
                    self.web_interface_presence.set_online(
                        web_interface_id, payload_text == "online"
                    )
                else:
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
            case _ if topic in PLAYER_INPUT_TOPICS:
                source_name = INPUT_SOURCE_NAME_BY_TOPIC.get(topic)
                if (
                    source_name is not None
                    and not self.input_source_control.is_allowed(source_name)
                ):
                    # Quelle gesperrt (siehe WEB_INTERFACE_INPUT_SOURCES_SET_TOPIC) — Eingabe
                    # wird komplett ignoriert, auch fürs gemeinsame panel_button_state.
                    print(f"Ignoring input from {source_name} because it is disabled.")
                    return
                panel_buttons = json.loads(payload_text)
                # Alles in Prozent (0-100) umrechnen, bevor es in den gemeinsamen
                # Zustand von Panel und Joy-Con eingeht: das Panel sendet 0/1
                # (gedrueckt = 100 %), der Joy-Con direkt 0-100.
                is_percent_source = topic == PLAYER_INPUT_JOYCON_TOPIC
                for button_name, button_value in panel_buttons.items():
                    if is_percent_source:
                        panel_buttons[button_name] = max(0, min(100, int(button_value)))
                    else:
                        panel_buttons[button_name] = 100 if button_value else 0
                self.panel_button_state.update(panel_buttons)

                # Gegenueberliegende Richtungen ergeben die Differenz (-100..100,
                # Vorzeichen = Richtung) und werden dann prozentual auf die in
                # config.ini eingestellte Standard-Geschwindigkeit der Achse skaliert.
                state = self.panel_button_state
                x_speed = self.move_controller.scale_speed(
                    "X", state.get("right", 0) - state.get("left", 0)
                )
                y_speed = self.move_controller.scale_speed(
                    "Y", state.get("back", 0) - state.get("front", 0)
                )
                z_speed = self.move_controller.scale_speed(
                    "Z", state.get("up", 0) - state.get("down", 0)
                )

                json_payload = json.dumps(
                    {
                        "X": x_speed,
                        "Y": y_speed,
                        "Z": z_speed,
                    }
                )
                self.mqtt_client.publish(
                    MOTOR_CONTROLLER_COMMAND_TOPIC, f"{json_payload}"
                )
            # 6) Steuerbefehl vom Webinterface (z.B. "left:80", "front:-80",
            #    "claw:open") — das Webinterface rechnet die Geschwindigkeit
            #    schon selbst aus (siehe app.component.ts), der Server muss
            #    hier nur noch den Tastennamen auf die Motor-Achse mappen.
            case _ if topic == WEB_INTERFACE_COMMAND_TOPIC:
                name, _, value = payload_text.strip().partition(":")

                if name == "claw":
                    self.mqtt_client.publish(
                        MOTOR_CONTROLLER_COMMAND_TOPIC, payload_text
                    )
                elif name in ("left", "right", "front", "back"):
                    # value = Eingabe in Prozent (-100..100, Vorzeichen = Richtung) —
                    # der Server skaliert sie auf die eingestellte XY-Geschwindigkeit.
                    try:
                        input_percent = float(value)
                    except ValueError:
                        print(f"Invalid movement value: {payload_text}")
                        return
                    axis = "X" if name in ("left", "right") else "Y"
                    self.mqtt_client.publish(
                        MOTOR_CONTROLLER_COMMAND_TOPIC,
                        f"{axis}:{self.move_controller.scale_speed(axis, input_percent)}",
                    )
                elif name in ("xy_speed", "z_speed"):
                    # Standard-Geschwindigkeit der Achsen in Prozent der max. Drehzahl
                    try:
                        speed_percent = int(float(value))
                    except ValueError:
                        print(f"Invalid speed value: {payload_text}")
                        return
                    self.move_controller.set_max_speed_percent(
                        "X" if name == "xy_speed" else "Z", speed_percent
                    )
                    self.publish_speed_settings()
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
                elif name == "claw_speed":
                    # Fahrgeschwindigkeit der Klaue in Grad/s, ebenfalls ein
                    # Settings-JSON (siehe "accel" oben). Wird gemerkt, damit sie
                    # nach einem Neustart des Motor-Controllers erneut gesendet wird.
                    try:
                        self.claw_speed_degrees_per_second = float(value)
                    except ValueError:
                        print(f"Invalid claw speed value: {value}")
                        return
                    self.publish_claw_speed()
                    self.publish_speed_settings()
                else:
                    print(f"Unknown webinterface command: {payload_text}")

    def main_loop(self):
        while True:
            time.sleep(1)
