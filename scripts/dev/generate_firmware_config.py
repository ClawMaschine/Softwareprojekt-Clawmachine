#!/usr/bin/env python3
"""
Generiert die geteilten Konfigurationswerte in include/firmware_config.h
aus config.ini (und config.local.ini falls vorhanden).

Verwendung:
    python scripts/dev/generate_firmware_config.py

Danach ESP-Firmware neu bauen:
    pio run -e claw_motor_controller
"""

from configparser import ConfigParser
from pathlib import Path

GENERATED_MARKER = "# GENERATED — nicht manuell bearbeiten, von generate_firmware_config.py erzeugt"

PROJECT_ROOT = Path(__file__).parent.parent.parent
CONFIG_INI = PROJECT_ROOT / "config.ini"
CONFIG_LOCAL_INI = PROJECT_ROOT / "config.local.ini"
FIRMWARE_CONFIG_H = PROJECT_ROOT / "include" / "firmware_config.h"


def load_config() -> ConfigParser:
    config = ConfigParser()
    config.read([CONFIG_INI, CONFIG_LOCAL_INI])
    return config


def read_firmware_config() -> str:
    return FIRMWARE_CONFIG_H.read_text(encoding="utf-8")


def replace_define(content: str, name: str, value: str) -> str:
    """Ersetzt den Wert eines #define in firmware_config.h."""
    lines = content.splitlines(keepends=True)
    result = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(f"#define {name} "):
            result.append(f'#define {name} {value}\n')
        else:
            result.append(line)
    return "".join(result)


def quote(value: str) -> str:
    return f'"{value}"'


def main() -> None:
    config = load_config()

    mqtt_broker = config.get("mqtt", "broker", fallback="localhost")
    mqtt_port = config.getint("mqtt", "port", fallback=1883)
    
    mqtt_user = config.get("mqtt", "user", fallback="")
    mqtt_password = config.get("mqtt", "password", fallback="")
    wifi_ssid = config.get("wifi", "ssid", fallback="")
    wifi_password = config.get("wifi", "password", fallback="")

    content = read_firmware_config()
    content = replace_define(content, "CLAW_MQTT_BROKER_HOST", quote(mqtt_broker))
    content = replace_define(content, "CLAW_MQTT_BROKER_PORT", str(mqtt_port))
    content = replace_define(content, "CLAW_MQTT_USER_USERNAME", str(mqtt_user))
    content = replace_define(content, "CLAW_MQTT_USER_USERNAME", str(mqtt_password))
    content = replace_define(content, "CLAW_CLIENT_WIFI_SSID", quote(wifi_ssid))
    content = replace_define(content, "CLAW_CLIENT_WIFI_PASSWORD", quote(wifi_password))

    FIRMWARE_CONFIG_H.write_text(content, encoding="utf-8")
    print(f"firmware_config.h aktualisiert:")
    print(f"  CLAW_MQTT_BROKER_HOST    = {quote(mqtt_broker)}")
    print(f"  CLAW_MQTT_BROKER_PORT    = {mqtt_port}")
    print(f"  CLAW_CLIENT_WIFI_SSID    = {quote(wifi_ssid)}")
    print(f"  CLAW_CLIENT_WIFI_PASSWORD = {'(gesetzt)' if wifi_password else '(leer)'}")
    print()

    if not wifi_password:
        print("Hinweis: wifi.password in config.ini ist leer — ESP kann sich nicht mit WiFi verbinden.")
    if mqtt_broker == "localhost":
        print("Hinweis: mqtt.broker ist 'localhost' — für ESP-Firmware sollte das die Server-IP sein.")
        print("  Für lokale Entwicklung: config.local.ini mit broker = 192.168.0.103 anlegen,")
        print("  dann dieses Skript erneut ausführen.")


if __name__ == "__main__":
    main()
