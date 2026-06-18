from abc import ABC, abstractmethod
from typing import Any, Optional, Tuple

# Eine Hardware-Eingabe wird als (action, value)-Paar an den ESP zurückgegeben.
# Der ESP ergänzt daraus zusammen mit seinem Namen ein vollständiges InputEvent.
InputSignal = Tuple[str, Optional[Any]]


class Hardware(ABC):
    """Abstrakte Oberklasse für jede an einem ESP angeschlossene Hardware."""

    # Wird von der Type-Map/Factory genutzt, um Hardware dynamisch zu erzeugen.
    hardware_type_name: str = "hardware"

    def __init__(self, name: str):
        self.name = name

    @abstractmethod
    def describe(self) -> str:
        """Menschenlesbare Beschreibung dieser Hardware."""


class InputHardware(Hardware, ABC):
    """Hardware, die Spieler-Eingaben erzeugt (z. B. Joy-Con, Buttons)."""

    @abstractmethod
    def interpret_message(self, payload: str) -> Optional[InputSignal]:
        """Wandelt eine eingehende MQTT-Payload in ein (action, value)-Signal um.

        Gibt None zurück, wenn die Payload keine verwertbare Eingabe enthält.
        """


class OutputHardware(Hardware, ABC):
    """Hardware, die durch eine Eingabe verändert wird (z. B. Motor, LED-Strip)."""

    @abstractmethod
    def build_command(self, action: str, value: Optional[Any] = None) -> str:
        """Übersetzt eine gewünschte Aktion in eine MQTT-Befehls-Payload."""


# --- Type-Map / Factory -----------------------------------------------------

HARDWARE_TYPES: dict[str, type[Hardware]] = {}


def register_hardware_type(hardware_class: type[Hardware]) -> type[Hardware]:
    """Klassen-Decorator: registriert eine Hardware-Klasse unter ihrem Typnamen.

    Neue Hardware-Typen lassen sich allein durch Dekorieren einer neuen Klasse
    ergänzen – ohne if/elif-Ketten in der Factory.
    """
    HARDWARE_TYPES[hardware_class.hardware_type_name] = hardware_class
    return hardware_class


def create_hardware(type_name: str, name: str) -> Optional[Hardware]:
    """Erzeugt ein Hardware-Objekt anhand seines Typnamens.

    Unbekannte Typen werden geloggt und übersprungen (Fallback: None).
    """
    hardware_class = HARDWARE_TYPES.get(type_name)
    if hardware_class is None:
        print(f"Unknown hardware type '{type_name}' – skipping hardware '{name}'")
        return None
    return hardware_class(name=name)


# --- Konkrete Eingabe-Hardware ---------------------------------------------


@register_hardware_type
class JoyCon(InputHardware):
    """Ganzer Nintendo Switch Joy-Con als Eingabegerät."""

    hardware_type_name = "joycon"

    def describe(self) -> str:
        return f"Joy-Con '{self.name}'"

    def interpret_message(self, payload: str) -> Optional[InputSignal]:
        action = payload.strip()
        if not action:
            return None
        return action, None


@register_hardware_type
class JoyConButton(InputHardware):
    """Einzelner Button eines Joy-Con."""

    hardware_type_name = "joycon_button"

    def describe(self) -> str:
        return f"Joy-Con button '{self.name}'"

    def interpret_message(self, payload: str) -> Optional[InputSignal]:
        is_pressed = payload.strip().lower() in ("1", "true", "pressed", "down")
        if not is_pressed:
            return None
        return self.name, True


@register_hardware_type
class GameController(InputHardware):
    """Generischer Game-Controller (z. B. Pro Controller)."""

    hardware_type_name = "game_controller"

    def describe(self) -> str:
        return f"Game controller '{self.name}'"

    def interpret_message(self, payload: str) -> Optional[InputSignal]:
        action = payload.strip()
        if not action:
            return None
        return action, None


# --- Konkrete Ausgabe-Hardware ---------------------------------------------


@register_hardware_type
class Motor(OutputHardware):
    """Ein einzelner Antriebsmotor einer Bewegungsachse."""

    hardware_type_name = "motor"

    def describe(self) -> str:
        return f"Motor '{self.name}'"

    def build_command(self, action: str, value: Optional[Any] = None) -> str:
        if value is None:
            return action
        return f"{action}:{value}"


@register_hardware_type
class LedStrip(OutputHardware):
    """Ein adressierbarer LED-Strip."""

    hardware_type_name = "led_strip"

    def describe(self) -> str:
        return f"LED strip '{self.name}'"

    def build_command(self, action: str, value: Optional[Any] = None) -> str:
        if value is None:
            return action
        return f"{action}:{value}"
