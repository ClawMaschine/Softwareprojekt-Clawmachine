import time
from typing import Optional

try:
    from python_server.clawmachine.esp_device import EspDevice
except ModuleNotFoundError:
    from esp_device import EspDevice

DEVICE_ADDED_TOPIC_PREFIX = "clawmachine/device/"
DEVICE_ADDED_TOPIC_SUFFIX = "/added"


class DeviceRegistry:
    def __init__(self, known_esp_names: list, device_added_topic: str):
        self.device_added_topic = device_added_topic
        self.devices_by_name: dict[str, EspDevice] = {
            name: EspDevice(name=name) for name in known_esp_names
        }

    def register(self, device_name: str) -> Optional[EspDevice]:
        cleaned = device_name.strip()
        if not cleaned:
            return None

        existing = self.devices_by_name.get(cleaned)
        if existing is not None:
            existing.added_at_unix_seconds = time.time()
            print(f"Device reconnected: {cleaned}")
            return existing

        new_device = EspDevice(name=cleaned, added_at_unix_seconds=time.time())
        self.devices_by_name[cleaned] = new_device
        print(f"Device added: {cleaned}")
        print(f"Known devices: {sorted(self.devices_by_name.keys())}")
        return new_device

    def get(self, name: str) -> Optional[EspDevice]:
        return self.devices_by_name.get(name)

    def extract_device_name(self, topic: str, payload_text: str) -> Optional[str]:
        if topic == self.device_added_topic:
            return payload_text

        if topic.startswith(DEVICE_ADDED_TOPIC_PREFIX) and topic.endswith(DEVICE_ADDED_TOPIC_SUFFIX):
            return topic[len(DEVICE_ADDED_TOPIC_PREFIX):-len(DEVICE_ADDED_TOPIC_SUFFIX)]

        return None
