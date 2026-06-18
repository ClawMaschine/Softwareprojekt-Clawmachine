from dataclasses import dataclass
from typing import Optional

# Hinweis: Die frühere `EspDevice`-Klasse wurde durch die ESP-Klassenhierarchie
# in `esp.py` (abstrakte `Esp`-Oberklasse + Unterklassen) ersetzt. Hier verbleibt
# nur noch das Metadaten-Wertobjekt, das `Esp` weiterverwendet.


@dataclass
class EspDeviceMetadata:
    uptime_milliseconds: Optional[int] = None
