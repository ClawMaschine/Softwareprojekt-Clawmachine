from dataclasses import dataclass, field
from typing import Optional


@dataclass
class EspDeviceMetadata:
    uptime_milliseconds: Optional[int] = None


@dataclass
class EspDevice:
    name: str
    added_at_unix_seconds: Optional[float] = None
    metadata: EspDeviceMetadata = field(default_factory=EspDeviceMetadata)
    is_online: bool = False

    def on_message(self, topic: str, payload: str):
        print(f"[{self.name}] Internal message on {topic}: {payload}")

    def to_dict(self) -> dict:
        # camelCase-Keys, weil das JSON direkt vom Webinterface (TypeScript)
        # konsumiert wird, siehe DEVICE_LIST_TOPIC in claw_machine.py.
        return {
            "name": self.name,
            "isOnline": self.is_online,
            "uptimeMilliseconds": self.metadata.uptime_milliseconds,
            "addedAtUnixSeconds": self.added_at_unix_seconds,
        }
