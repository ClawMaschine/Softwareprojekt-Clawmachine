import json
from dataclasses import dataclass, field
from typing import List

GENERIC_ESP_TYPE_NAME = "generic"


@dataclass
class HardwareDescriptor:
    """Beschreibt eine einzelne Hardware aus einer Discovery-Nachricht."""

    name: str
    type: str


@dataclass
class EspDescriptor:
    """Beschreibt einen ESP samt seiner Hardware aus einer Discovery-Nachricht."""

    name: str
    type: str = GENERIC_ESP_TYPE_NAME
    hardware: List[HardwareDescriptor] = field(default_factory=list)


def parse_device_added_payload(payload: str) -> EspDescriptor:
    """Wandelt eine `device/added`-Payload in einen EspDescriptor um.

    Unterstützt zwei Formate:
    - JSON-Objekt ``{"name": ..., "type": ..., "hardware": [{"name", "type"}]}``
    - reiner Name-String (abwärtskompatibel) → Generic-ESP ohne Hardware
    """
    cleaned = payload.strip()

    try:
        raw = json.loads(cleaned)
    except (json.JSONDecodeError, ValueError):
        raw = None

    if not isinstance(raw, dict):
        return EspDescriptor(name=cleaned)

    name = str(raw.get("name", "")).strip()
    esp_type = str(raw.get("type", GENERIC_ESP_TYPE_NAME)).strip() or GENERIC_ESP_TYPE_NAME

    hardware_descriptors: List[HardwareDescriptor] = []
    for hardware_entry in raw.get("hardware", []) or []:
        if not isinstance(hardware_entry, dict):
            continue
        hardware_name = str(hardware_entry.get("name", "")).strip()
        hardware_type = str(hardware_entry.get("type", "")).strip()
        if hardware_name and hardware_type:
            hardware_descriptors.append(
                HardwareDescriptor(name=hardware_name, type=hardware_type)
            )

    return EspDescriptor(name=name, type=esp_type, hardware=hardware_descriptors)
