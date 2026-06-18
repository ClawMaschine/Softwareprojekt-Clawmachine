from dataclasses import dataclass
from typing import Any, Optional


@dataclass
class InputEvent:
    """Ein vom Spieler ausgelöstes Eingabe-Ereignis.

    Wird von einer InputHardware (z. B. Joy-Con) erzeugt und über den
    PlayerInputEsp an die ClawMachine (Zentrale) weitergereicht. Input- und
    Output-Objekte kennen sich dadurch nicht direkt – die ClawMachine
    entscheidet, welche Reaktion (z. B. Motorbefehl) ein Event auslöst.
    """

    source_esp_name: str
    hardware_name: str
    action: str
    value: Optional[Any] = None
