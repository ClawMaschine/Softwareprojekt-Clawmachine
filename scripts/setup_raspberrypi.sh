#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_info "Aktualisiere Paketlisten..."
if [[ "$EUID" -eq 0 ]]; then
    apt-get update
    apt-get upgrade -y
else
    sudo apt-get update
    sudo apt-get upgrade -y
fi

print_info "Installiere Docker und Docker Compose..."
if [[ "$EUID" -eq 0 ]]; then
    apt-get install -y docker.io docker-compose-plugin
else
    sudo apt-get install -y docker.io docker-compose-plugin
fi

print_info "Installiere Python3, pip und venv..."
if [[ "$EUID" -eq 0 ]]; then
    apt-get install -y python3 python3-pip python3-venv mosquitto-clients
else
    sudo apt-get install -y python3 python3-pip python3-venv mosquitto-clients
fi

print_info "Fuege aktuellen Benutzer zur docker-Gruppe hinzu..."
if [[ "$EUID" -ne 0 ]]; then
    sudo usermod -aG docker "$USER"
    printf 'Hinweis: Damit Docker ohne sudo funktioniert, melde dich ab und wieder an (oder reboot).\n'
else
    printf 'Warnung: Skript laeuft als root. Bitte fuege den gewuenschten Benutzer manuell zur docker-Gruppe hinzu.\n'
fi

print_info "Erstelle Python-Virtualenv und installiere Abhaengigkeiten..."
venv_path="$repository_root_directory/.venv"

if [[ ! -d "$venv_path" ]]; then
    python3 -m venv "$venv_path"
    printf 'Virtualenv erstellt unter: %s\n' "$venv_path"
fi

# shellcheck disable=SC1091
source "$venv_path/bin/activate"
pip install --upgrade pip
pip install -r "$repository_root_directory/python_server/requirements.txt"

print_info "Setup abgeschlossen!"
printf '\nZusammenfassung:\n'
printf '  - Docker & Docker Compose installiert\n'
printf '  - Python-Abhaengigkeiten in %s installiert\n' "$venv_path"
printf '  - mosquitto-clients installiert (fuer MQTT-Debugging)\n'
printf '\nNaechste Schritte:\n'
printf '  1. Abmelden und wieder anmelden (damit docker-Gruppe wirksam wird)\n'
printf '  2. MQTT-Broker starten: ./scripts/start_mqtt_broker.sh\n'
printf '  3. Python-Server starten: source .venv/bin/activate && python -m python_server.server\n'
