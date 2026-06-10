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
