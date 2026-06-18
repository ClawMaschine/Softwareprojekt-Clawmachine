import time
from abc import ABC, abstractmethod
from typing import Any, Callable, ClassVar, List, Optional

try:
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.esp_device import EspDeviceMetadata
    from python_server.clawmachine.hardware import (
        Hardware,
        InputHardware,
        OutputHardware,
        create_hardware,
    )
    from python_server.clawmachine.input_event import InputEvent
    from python_server.clawmachine.esp_descriptor import EspDescriptor
except ModuleNotFoundError:
    from mqtt import MQTTClient
    from clawmachine.esp_device import EspDeviceMetadata
    from clawmachine.hardware import (
        Hardware,
        InputHardware,
        OutputHardware,
        create_hardware,
    )
    from clawmachine.input_event import InputEvent
    from clawmachine.esp_descriptor import EspDescriptor

InputEventListener = Callable[[InputEvent], None]


class Esp(ABC):
    """Abstrakte Oberklasse für jeden ESP32-Mikrocontroller der Clawmachine.

    Kapselt die Standard-MQTT-Funktionalität (Identifikation, Online-Status,
    Uptime-Metadaten, Senden von Befehlen, Empfangen von Nachrichten) und baut
    dabei auf der bestehenden :class:`MQTTClient`-Klasse sowie den
    :class:`EspDeviceMetadata` auf. Jeder ESP hält eine veränderbare Liste von
    Hardware-Objekten. Konkrete Controller-Typen erben hiervon.
    """

    # Diskriminator für Discovery/Factory. Unterklassen überschreiben diesen Wert.
    esp_type_name: ClassVar[str] = "esp"

    def __init__(
        self,
        name: str,
        mqtt_client: MQTTClient,
        command_topic: Optional[str] = None,
    ):
        self.name = name
        self.mqtt_client = mqtt_client
        self.metadata = EspDeviceMetadata()
        self.is_online = False
        self.added_at_unix_seconds: Optional[float] = None
        self.hardware_components: List[Hardware] = []
        self.command_topic = command_topic or f"clawmachine/{name}/command"
        self._input_event_listener: Optional[InputEventListener] = None

    # --- Hardware-Verwaltung (dynamische Liste) ----------------------------

    def add_hardware(self, hardware: Hardware) -> None:
        self.hardware_components.append(hardware)

    def remove_hardware(self, hardware: Hardware) -> None:
        if hardware in self.hardware_components:
            self.hardware_components.remove(hardware)

    def input_hardware_components(self) -> List[InputHardware]:
        return [
            hardware
            for hardware in self.hardware_components
            if isinstance(hardware, InputHardware)
        ]

    def output_hardware_components(self) -> List[OutputHardware]:
        return [
            hardware
            for hardware in self.hardware_components
            if isinstance(hardware, OutputHardware)
        ]

    # --- MQTT-Kommunikation ------------------------------------------------

    def send_command(self, command: str) -> bool:
        """Sendet einen Befehl auf das Befehls-Topic dieses ESP."""
        return self.mqtt_client.publish(self.command_topic, command)

    def on_message(self, topic: str, payload: str) -> None:
        """Verarbeitet eine an diesen ESP gerichtete Nachricht.

        Standardverhalten: protokollieren. Unterklassen erweitern dies, z. B.
        um Eingaben zu interpretieren und als InputEvent zu emittieren.
        """
        print(f"[{self.name}] message on {topic}: {payload}")

    # --- Lose Kopplung über die Zentrale -----------------------------------

    def set_input_listener(self, listener: Optional[InputEventListener]) -> None:
        """Registriert den Listener (die ClawMachine), der Eingaben verarbeitet."""
        self._input_event_listener = listener

    def emit_input_event(self, event: InputEvent) -> None:
        if self._input_event_listener is not None:
            self._input_event_listener(event)

    @abstractmethod
    def describe(self) -> str:
        """Menschenlesbare Beschreibung dieses ESP samt Rolle."""


# --- Type-Map / Factory -----------------------------------------------------

ESP_TYPES: dict[str, type[Esp]] = {}


def register_esp_type(esp_class: type[Esp]) -> type[Esp]:
    """Klassen-Decorator: registriert eine ESP-Klasse unter ihrem Typnamen.

    Neue ESP-Typen lassen sich allein durch Dekorieren einer neuen Klasse
    ergänzen – ohne if/elif-Ketten in der Factory.
    """
    ESP_TYPES[esp_class.esp_type_name] = esp_class
    return esp_class


def create_esp(descriptor: EspDescriptor, mqtt_client: MQTTClient) -> Esp:
    """Erzeugt das passende ESP-Objekt anhand eines Discovery-Descriptors.

    Wählt die ESP-Klasse über die Type-Map (Fallback: GenericEsp) und hängt die
    im Descriptor beschriebene Hardware über die Hardware-Factory an.
    """
    esp_class = ESP_TYPES.get(descriptor.type, GenericEsp)
    esp = esp_class(name=descriptor.name, mqtt_client=mqtt_client)
    esp.added_at_unix_seconds = time.time()
    for hardware_descriptor in descriptor.hardware:
        hardware = create_hardware(hardware_descriptor.type, hardware_descriptor.name)
        if hardware is not None:
            esp.add_hardware(hardware)
    return esp


# --- Konkrete ESP-Typen -----------------------------------------------------


@register_esp_type
class MotorControllerEsp(Esp):
    """Steuert eine oder mehrere Bewegungsachsen über Motor-Hardware."""

    esp_type_name = "motor_controller"

    def describe(self) -> str:
        return f"Motor controller '{self.name}'"

    def drive(self, action: str, value: Optional[Any] = None) -> bool:
        """Übersetzt eine Bewegung über die Motor-Hardware in einen MQTT-Befehl."""
        motors = self.output_hardware_components()
        if motors:
            payload = motors[0].build_command(action, value)
        elif value is None:
            payload = action
        else:
            payload = f"{action}:{value}"
        return self.send_command(payload)


@register_esp_type
class CameraEsp(Esp):
    """ESP mit angeschlossener Kamera-Hardware."""

    esp_type_name = "camera"

    def describe(self) -> str:
        return f"Camera ESP '{self.name}'"


@register_esp_type
class PlayerInputEsp(Esp):
    """ESP, der Spieler-Eingaben (Joy-Con / Game-Controller) entgegennimmt."""

    esp_type_name = "player_input"

    def describe(self) -> str:
        return f"Player input ESP '{self.name}'"

    def on_message(self, topic: str, payload: str) -> None:
        super().on_message(topic, payload)
        for hardware in self.input_hardware_components():
            signal = hardware.interpret_message(payload)
            if signal is None:
                continue
            action, value = signal
            self.emit_input_event(
                InputEvent(
                    source_esp_name=self.name,
                    hardware_name=hardware.name,
                    action=action,
                    value=value,
                )
            )


@register_esp_type
class ControlPanelEsp(Esp):
    """Steuereinheit / Control Panel der Maschine."""

    esp_type_name = "control_panel"

    def describe(self) -> str:
        return f"Control panel ESP '{self.name}'"


@register_esp_type
class GenericEsp(Esp):
    """Fallback für unbekannte ESP-Typen oder reine Name-Strings (Discovery)."""

    esp_type_name = "generic"

    def describe(self) -> str:
        return f"Generic ESP '{self.name}'"
