import time
from typing import Callable, List, Optional, Type, TypeVar

try:
    from python_server.mqtt import MQTTClient
    from python_server.clawmachine.esp import Esp, create_esp
    from python_server.clawmachine.esp_descriptor import (
        EspDescriptor,
        parse_device_added_payload,
    )
except ModuleNotFoundError:
    from mqtt import MQTTClient
    from clawmachine.esp import Esp, create_esp
    from clawmachine.esp_descriptor import EspDescriptor, parse_device_added_payload

DEVICE_ADDED_TOPIC_PREFIX = "clawmachine/device/"
DEVICE_ADDED_TOPIC_SUFFIX = "/added"

EspType = TypeVar("EspType", bound=Esp)


class DeviceRegistry:
    """Verwaltet die per MQTT entdeckten ESP-Objekte (Discovery/Registrierung).

    Erzeugt für jeden registrierten Controller über die Factory das passende
    Esp-Objekt samt seiner Hardware und meldet neue ESPs an die Zentrale.
    """

    def __init__(
        self,
        mqtt_client: MQTTClient,
        device_added_topic: str,
        on_esp_registered: Optional[Callable[[Esp], None]] = None,
        known_esp_descriptors: Optional[List[EspDescriptor]] = None,
    ):
        self.mqtt_client = mqtt_client
        self.device_added_topic = device_added_topic
        self.on_esp_registered = on_esp_registered
        self.esps_by_name: dict[str, Esp] = {}

        for descriptor in known_esp_descriptors or []:
            self._create_and_store(descriptor)

    # --- Registrierung -----------------------------------------------------

    def register_from_payload(self, topic: str, payload_text: str) -> Optional[Esp]:
        """Registriert einen ESP aus einer eingehenden `device/added`-Nachricht."""
        if topic == self.device_added_topic:
            descriptor = parse_device_added_payload(payload_text)
        else:
            name_from_topic = self._extract_name_from_added_topic(topic)
            if name_from_topic is None:
                return None
            descriptor = parse_device_added_payload(payload_text)
            # Der Name stammt beim Per-Gerät-Topic aus dem Topic, nicht der Payload.
            descriptor.name = name_from_topic

        if not descriptor.name:
            return None
        return self._create_and_store(descriptor)

    def _create_and_store(self, descriptor: EspDescriptor) -> Esp:
        existing = self.esps_by_name.get(descriptor.name)
        if existing is not None:
            existing.added_at_unix_seconds = time.time()
            print(f"Device reconnected: {descriptor.name}")
            return existing

        esp = create_esp(descriptor, self.mqtt_client)
        self.esps_by_name[descriptor.name] = esp
        if self.on_esp_registered is not None:
            self.on_esp_registered(esp)
        print(f"Device added: {descriptor.name} ({esp.describe()})")
        print(f"Known devices: {sorted(self.esps_by_name.keys())}")
        return esp

    # --- Zugriff -----------------------------------------------------------

    def get(self, name: str) -> Optional[Esp]:
        return self.esps_by_name.get(name)

    def all_esps(self) -> List[Esp]:
        return list(self.esps_by_name.values())

    def esps_of_type(self, esp_class: Type[EspType]) -> List[EspType]:
        return [esp for esp in self.esps_by_name.values() if isinstance(esp, esp_class)]

    # --- Topic-Parsing -----------------------------------------------------

    def extract_device_name(self, topic: str, payload_text: str) -> Optional[str]:
        if topic == self.device_added_topic:
            return payload_text
        return self._extract_name_from_added_topic(topic)

    @staticmethod
    def _extract_name_from_added_topic(topic: str) -> Optional[str]:
        if topic.startswith(DEVICE_ADDED_TOPIC_PREFIX) and topic.endswith(
            DEVICE_ADDED_TOPIC_SUFFIX
        ):
            return topic[len(DEVICE_ADDED_TOPIC_PREFIX) : -len(DEVICE_ADDED_TOPIC_SUFFIX)]
        return None
