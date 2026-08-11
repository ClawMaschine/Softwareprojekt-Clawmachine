#!/usr/bin/env python3
"""
Generiert frontend/src/environments/environment.ts aus config.ini
(und config.local.ini falls vorhanden).

Verwendung:
    python scripts/dev/generate_frontend_environment.py

Wird auch automatisch im Docker-Build des Frontends ausgeführt
(siehe docker/frontend/Dockerfile), bevor `npm run build` läuft.
"""

from configparser import ConfigParser
from pathlib import Path

PROJECT_ROOT = Path(__file__).parent.parent.parent
CONFIG_INI = PROJECT_ROOT / "config.ini"
CONFIG_LOCAL_INI = PROJECT_ROOT / "config.local.ini"
ENVIRONMENT_TS = PROJECT_ROOT / "frontend" / "src" / "environments" / "environment.ts"


def load_config() -> ConfigParser:
    config = ConfigParser()
    config.read([CONFIG_INI, CONFIG_LOCAL_INI])
    return config


def read_environment_ts() -> str:
    return ENVIRONMENT_TS.read_text(encoding="utf-8")


def replace_field(content: str, name: str, value: str) -> str:
    """Ersetzt den Wert eines Felds im environment-Objekt (z.B. `mqttUsername: '...',`)."""
    lines = content.splitlines(keepends=True)
    result = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(f"{name}:"):
            indent = line[: len(line) - len(line.lstrip())]
            result.append(f"{indent}{name}: {value},\n")
        else:
            result.append(line)
    return "".join(result)


def quote(value: str) -> str:
    return "'" + value.replace("\\", "\\\\").replace("'", "\\'") + "'"


def main() -> None:
    config = load_config()

    mqtt_websocket_port = config.getint("mqtt", "websocket_port", fallback=9001)
    mqtt_username = config.get("mqtt", "username", fallback="")
    mqtt_password = config.get("mqtt", "password", fallback="")

    content = read_environment_ts()
    content = replace_field(content, "mqttWebsocketPort", str(mqtt_websocket_port))
    content = replace_field(content, "mqttUsername", quote(mqtt_username))
    content = replace_field(content, "mqttPassword", quote(mqtt_password))

    ENVIRONMENT_TS.write_text(content, encoding="utf-8")
    print("environment.ts aktualisiert:")
    print(f"  mqttWebsocketPort = {mqtt_websocket_port}")
    print(f"  mqttUsername      = {quote(mqtt_username)}")
    print(f"  mqttPassword      = {'(gesetzt)' if mqtt_password else '(leer)'}")


if __name__ == "__main__":
    main()
