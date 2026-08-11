import time
from typing import Optional

try:
    from python_server.clawmachine.esp_device import EspDevice
except ModuleNotFoundError:
    from esp_device import EspDevice




class DeviceRegistry:
    def __init__(self, topic_prefix: str):
        self.devices_by_name: dict[str, EspDevice] = {}
        self.topic_prefix = topic_prefix

    def add(self, device_name: str) -> Optional[EspDevice]:
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
    
    
