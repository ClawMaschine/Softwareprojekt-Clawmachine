"""
Gemeinsamer Helfer für setup_server.py und run_project.py:
legt die Mosquitto-Passwortdatei an, falls sie fehlt.

mosquitto.conf verlangt password_file (allow_anonymous false) — ohne diese
Datei kommt der Broker-Container gar nicht erst hoch (Restart-Schleife,
"Unable to open pwfile").
"""

import subprocess
import sys
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
MOSQUITTO_CONFIG_DIRECTORY = _REPO_ROOT / "docker" / "mosquitto" / "config"
MOSQUITTO_PASSWORDS_PATH = MOSQUITTO_CONFIG_DIRECTORY / "passwords"


def mosquitto_password_file_exists() -> bool:
    return MOSQUITTO_PASSWORDS_PATH.exists()


def ensure_mosquitto_password_file(console, stream_command) -> bool:
    """Erzeugt die Passwortdatei, falls sie fehlt. True, wenn sie danach existiert.

    console:        rich.console.Console des aufrufenden Skripts
    stream_command: Funktion (cmd: list[str], title: str) -> bool, die einen
                    Befehl mit Log-Ausgabe ausführt (_stream in den Skripten)
    """
    if mosquitto_password_file_exists():
        console.print("\n  [green]✓ Mosquitto-Passwortdatei bereits vorhanden[/]")
        return True

    console.print("\n  [bold dim]▸ Mosquitto-Passwortdatei erzeugen (nicht in Git, siehe .gitignore)[/]")

    # Zugangsdaten aus config.ini/config.local.ini wiederverwenden statt hier
    # erneut zu parsen — siehe python_server/configuration_loader.py.
    credentials_result = subprocess.run(
        [
            sys.executable, "-c",
            "from python_server.configuration_loader import load_mqtt_configuration\n"
            "mqtt_configuration = load_mqtt_configuration()\n"
            "print(mqtt_configuration.username)\n"
            "print(mqtt_configuration.password)\n",
        ],
        cwd=_REPO_ROOT,
        capture_output=True,
        text=True,
    )
    lines = credentials_result.stdout.strip().splitlines()
    if credentials_result.returncode != 0 or len(lines) < 2:
        console.print("  [red]✗ Konnte MQTT-Zugangsdaten nicht aus config.ini lesen[/]")
        console.print(f"  [dim]{credentials_result.stderr.strip()}[/]")
        return False

    mqtt_username, mqtt_password = lines[0], lines[1]

    # chmod im selben Container: die Datei gehört danach root, und der Broker
    # (Benutzer "mosquitto") muss sie lesen können — von außen ginge das nur als root.
    created = stream_command(
        [
            "docker", "run", "--rm",
            "-v", f"{MOSQUITTO_CONFIG_DIRECTORY}:/mosquitto/config",
            "--entrypoint", "sh",
            "eclipse-mosquitto:latest",
            "-c",
            'mosquitto_passwd -b -c /mosquitto/config/passwords "$0" "$1"'
            " && chmod 644 /mosquitto/config/passwords",
            mqtt_username, mqtt_password,
        ],
        f"Mosquitto-Passwortdatei erzeugen (Benutzer '{mqtt_username}')",
    )
    if not created:
        return False

    return mosquitto_password_file_exists()
