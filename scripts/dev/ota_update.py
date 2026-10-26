"""Baut die Firmware eines Boards und stoesst das OTA-Update per MQTT an.

Beispiel:
    python scripts/dev/ota_update.py claw_motor_controller

Ablauf: Firmware im Docker-Container bauen (Image clawmachine-pio-builder, wird beim
ersten Aufruf gebaut; --host-pio nutzt stattdessen das lokal installierte PlatformIO)
-> firmware.bin nach firmware_updates/<env>.bin kopieren (wird vom Python-Server unter
/firmware/<env>.bin ausgeliefert) -> auf clawmachine/<geraetename>/ota die Download-URL
publizieren -> auf clawmachine/<geraetename>/ota/status warten.

Mit --no-build wird nicht gebaut, sondern eine dort schon liegende .bin verwendet.

Der Geraetename ist die MQTT-Client-ID des Boards (CLAW_*_CLIENT_ID in
platformio.ini). Die Download-URL nutzt standardmaessig den Broker-Host aus
config.ini — das ESP akzeptiert nur genau diesen Host.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from configparser import ConfigParser
from pathlib import Path

try:
    import paho.mqtt.client as mqtt
except ModuleNotFoundError:
    sys.exit(
        "paho-mqtt fehlt. Im Projekt-venv ausfuehren:\n"
        "  .venv/bin/python scripts/dev/ota_update.py <environment>\n"
        "oder Abhaengigkeiten installieren: scripts/setup/install_python_dependencies.sh"
    )

PROJECT_ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_UPDATES_DIRECTORY = PROJECT_ROOT / "firmware_updates"

PLATFORMIO_BUILDER_IMAGE = "clawmachine-pio-builder"
PLATFORMIO_BUILDER_CACHE_VOLUME = "clawmachine-pio-cache"
# Eigenes PlatformIO-Workspace im Repo fuer Docker-Builds, damit das .pio des
# Hosts (andere Pfade, andere Toolchain) nicht ueberschrieben wird.
DOCKER_WORKSPACE_DIRECTORY_NAME = ".pio-docker"


def build_firmware_in_docker(environment: str) -> None:
    if shutil.which("docker") is None:
        sys.exit("docker nicht gefunden – bitte setup_server.py ausfuehren oder mit --host-pio lokal bauen.")

    dockerfile_directory = PROJECT_ROOT / "docker" / "platformio_builder"
    if subprocess.run(["docker", "image", "inspect", PLATFORMIO_BUILDER_IMAGE], capture_output=True).returncode != 0:
        print(f"Baue Image '{PLATFORMIO_BUILDER_IMAGE}' (einmalig) ...")
        if subprocess.run(["docker", "build", "-t", PLATFORMIO_BUILDER_IMAGE, str(dockerfile_directory)]).returncode != 0:
            sys.exit("Build des PlatformIO-Images fehlgeschlagen.")

    print(f"Baue Firmware '{environment}' im Container (erster Lauf laedt die Toolchain, das dauert) ...")
    command = [
        "docker", "run", "--rm",
        # UID/GID des Aufrufers: sonst gehoeren die Build-Dateien im Repo root
        "--user", f"{os.getuid()}:{os.getgid()}",
        "-e", f"PLATFORMIO_WORKSPACE_DIR=/workspace/{DOCKER_WORKSPACE_DIRECTORY_NAME}",
        "-e", "HOME=/tmp",
        "-v", f"{PROJECT_ROOT}:/workspace",
        "-v", f"{PLATFORMIO_BUILDER_CACHE_VOLUME}:/pio-cache",
        PLATFORMIO_BUILDER_IMAGE,
        "pio", "run", "-e", environment,
    ]
    if subprocess.run(command).returncode != 0:
        sys.exit("Build im Container fehlgeschlagen.")


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
    parser.add_argument("--timeout", type=int, default=180, help="Sekunden auf Status-Antwort des Geraets warten (Standard: 180)")
    parser.add_argument("--no-build", action="store_true", help="Nicht neu bauen, vorhandenes firmware.bin nutzen")
    parser.add_argument("--host-pio", action="store_true", help="Statt im Docker-Container mit dem lokal installierten PlatformIO bauen")
    arguments = parser.parse_args()

    config = ConfigParser()
    config.read([PROJECT_ROOT / "config.ini", PROJECT_ROOT / "config.local.ini"])
    broker_host = config.get("mqtt", "broker")
    broker_port = config.getint("mqtt", "port", fallback=1883)
    username = config.get("mqtt", "username", fallback=None)
    password = config.get("mqtt", "password", fallback=None)

    device_name = arguments.device or read_client_id_from_platformio(arguments.environment)
    download_host = arguments.download_host or broker_host

    published_binary = FIRMWARE_UPDATES_DIRECTORY / f"{arguments.environment}.bin"
    build_directory = PROJECT_ROOT / (".pio" if arguments.host_pio else DOCKER_WORKSPACE_DIRECTORY_NAME)
    built_binary = build_directory / "build" / arguments.environment / "firmware.bin"

    if not arguments.no_build:
        if arguments.host_pio:
            if shutil.which("pio") is None:
                sys.exit("PlatformIO (pio) nicht gefunden – ohne --host-pio wird im Docker-Container gebaut.")
            if subprocess.run(["pio", "run", "-e", arguments.environment], cwd=PROJECT_ROOT).returncode != 0:
                sys.exit("Build fehlgeschlagen.")
        else:
            build_firmware_in_docker(arguments.environment)

    # --no-build: bereits abgelegte .bin im Server-Ordner bevorzugen, sonst die frisch gebaute nehmen
    if arguments.no_build and published_binary.exists():
        print(f"Nutze vorhandene {published_binary.relative_to(PROJECT_ROOT)}")
    elif built_binary.exists():
        FIRMWARE_UPDATES_DIRECTORY.mkdir(exist_ok=True)
        shutil.copy(built_binary, published_binary)
    else:
        sys.exit(f"Keine Firmware gefunden: weder {published_binary} noch {built_binary}.")

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
    client.loop_start()
    waited_seconds = 0
    while result["state"] is None and waited_seconds < arguments.timeout:
        time.sleep(1)
        waited_seconds += 1
    client.loop_stop()
    if result["state"] is None:
        print(f"Keine Antwort von '{device_name}' nach {arguments.timeout} s — laeuft auf dem Board schon eine Firmware mit OTA?")
    sys.exit(0 if result["state"] == "success" else 1)


if __name__ == "__main__":
    main()
