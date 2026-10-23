"""Baut die Firmware eines Boards und stoesst das OTA-Update per MQTT an.

Beispiel:
    python scripts/dev/ota_update.py claw_motor_controller

Ablauf: pio run -e <env> -> firmware.bin nach firmware_updates/<env>.bin kopieren
(wird vom Python-Server unter /firmware/<env>.bin ausgeliefert) -> auf
clawmachine/<geraetename>/ota die Download-URL publizieren -> auf
clawmachine/<geraetename>/ota/status warten.

Der Geraetename ist die MQTT-Client-ID des Boards (CLAW_*_CLIENT_ID in
platformio.ini). Die Download-URL nutzt standardmaessig den Broker-Host aus
config.ini — das ESP akzeptiert nur genau diesen Host.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from configparser import ConfigParser
from pathlib import Path

import paho.mqtt.client as mqtt

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_UPDATES_DIRECTORY = PROJECT_ROOT / "firmware_updates"


def read_client_id_from_platformio(environment: str) -> str:
    section = re.search(
        rf"^\[env:{re.escape(environment)}\](.*?)(?=^\[|\Z)",
        (PROJECT_ROOT / "platformio.ini").read_text(),
        re.S | re.M,
    )
    if section is None:
        sys.exit(f"Environment '{environment}' nicht in platformio.ini gefunden.")
    match = re.search(r'-DCLAW_\w*CLIENT_ID=\\"([^"\\]+)\\"', section.group(1))
    if match is None:
        sys.exit(f"Keine CLAW_*_CLIENT_ID in [env:{environment}] gefunden — mit --device angeben.")
    return match.group(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("environment", help="PlatformIO-Environment, z.B. claw_motor_controller")
    parser.add_argument("--device", help="MQTT-Geraetename (Standard: Client-ID aus platformio.ini)")
    parser.add_argument("--download-host", help="Host in der Download-URL (Standard: [mqtt] broker aus config.ini)")
    parser.add_argument("--download-port", type=int, default=8000, help="HTTP-Port des Servers (Standard: 8000)")
    parser.add_argument("--no-build", action="store_true", help="Nicht neu bauen, vorhandenes firmware.bin nutzen")
    arguments = parser.parse_args()

    config = ConfigParser()
    config.read([PROJECT_ROOT / "config.ini", PROJECT_ROOT / "config.local.ini"])
    broker_host = config.get("mqtt", "broker")
    broker_port = config.getint("mqtt", "port", fallback=1883)
    username = config.get("mqtt", "username", fallback=None)
    password = config.get("mqtt", "password", fallback=None)

    device_name = arguments.device or read_client_id_from_platformio(arguments.environment)
    download_host = arguments.download_host or broker_host

    if not arguments.no_build:
        if subprocess.run(["pio", "run", "-e", arguments.environment], cwd=PROJECT_ROOT).returncode != 0:
            sys.exit("Build fehlgeschlagen.")

    built_binary = PROJECT_ROOT / ".pio" / "build" / arguments.environment / "firmware.bin"
    if not built_binary.exists():
        sys.exit(f"{built_binary} nicht gefunden.")
    FIRMWARE_UPDATES_DIRECTORY.mkdir(exist_ok=True)
    shutil.copy(built_binary, FIRMWARE_UPDATES_DIRECTORY / f"{arguments.environment}.bin")

    url = f"http://{download_host}:{arguments.download_port}/firmware/{arguments.environment}.bin"
    ota_topic = f"clawmachine/{device_name}/ota"
    status_topic = f"{ota_topic}/status"

    client = mqtt.Client(client_id="ota_update_script")  # paho-mqtt 1.x, siehe requirements.txt
    if username:
        client.username_pw_set(username, password)
    final_states = {"success", "error"}
    result = {"state": None}

    def on_connect(client, _userdata, _flags, _return_code):
        client.subscribe(status_topic)
        client.publish(ota_topic, json.dumps({"url": url}))
        print(f"OTA-Befehl an {ota_topic}: {url}")

    def on_message(client, _userdata, message):
        status = json.loads(message.payload)
        print(f"[{status.get('state')}] {status.get('detail')}")
        if status.get("state") in final_states:
            result["state"] = status["state"]
            client.disconnect()

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(broker_host, broker_port)
    client.loop_forever()
    sys.exit(0 if result["state"] == "success" else 1)


if __name__ == "__main__":
    main()
