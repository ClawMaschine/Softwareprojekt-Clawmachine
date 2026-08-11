#!/usr/bin/env bash
# Server-Setup (Ubuntu x64).
# Für lokale Entwicklung: scripts/setup/init_project.sh
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"

print_info() {
    printf '\n==> %s\n' "$1"
}

run_as_root() {
    if [[ "$EUID" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

print_info "Server-Setup (Ubuntu x64)"

print_info "Aktualisiere Paketlisten"
run_as_root apt-get update
run_as_root apt-get upgrade -y

print_info "Installiere Docker und Docker Compose (offizielle apt-Quelle)"
run_as_root apt-get install -y ca-certificates curl
run_as_root install -m 0755 -d /etc/apt/keyrings
run_as_root curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
run_as_root chmod a+r /etc/apt/keyrings/docker.asc

run_as_root tee /etc/apt/sources.list.d/docker.sources > /dev/null <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF

run_as_root apt-get update
run_as_root apt-get install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin

print_info "Installiere Python und mosquitto-clients"
run_as_root apt-get install -y python3 python3-pip python3-venv mosquitto-clients

print_info "Füge aktuellen Benutzer zur docker-Gruppe hinzu"
if [[ "$EUID" -ne 0 ]]; then
    run_as_root usermod -aG docker "$USER"
    printf 'Hinweis: Damit Docker ohne sudo funktioniert, einmal ab- und wieder anmelden.\n'
else
    printf 'Warnung: Skript läuft als root. Benutzer manuell zur docker-Gruppe hinzufügen.\n'
fi

print_info "Installiere Python-Abhängigkeiten"
"$script_directory/install_python_dependencies.sh"

print_info "Richte WLAN-Hotspot ein (Access Point für ESP32-Boards)"
run_as_root python3 "$script_directory/setup_hotspot.py"

mosquitto_passwords_path="$repository_root_directory/docker/mosquitto/config/passwords"

if [[ -f "$mosquitto_passwords_path" ]]; then
    print_info "Mosquitto-Passwortdatei bereits vorhanden"
else
    print_info "Erzeuge Mosquitto-Passwortdatei (nicht in Git, siehe .gitignore)"

    # Zugangsdaten aus config.ini/config.local.ini wiederverwenden statt hier
    # erneut zu parsen — siehe python_server/configuration_loader.py.
    mqtt_credentials="$(cd "$repository_root_directory" && python3 -c "
from python_server.configuration_loader import load_mqtt_configuration
mqtt_configuration = load_mqtt_configuration()
print(mqtt_configuration.username)
print(mqtt_configuration.password)
")"
    mqtt_username="$(sed -n '1p' <<< "$mqtt_credentials")"
    mqtt_password="$(sed -n '2p' <<< "$mqtt_credentials")"

    docker run --rm \
        -v "$repository_root_directory/docker/mosquitto/config:/mosquitto/config" \
        eclipse-mosquitto:latest \
        mosquitto_passwd -b -c /mosquitto/config/passwords "$mqtt_username" "$mqtt_password"

    printf 'Mosquitto-Passwortdatei erzeugt für Benutzer "%s".\n' "$mqtt_username"
fi

print_info "Setze berechtigungen für die Mosquitto-Konfigurationsdateien"
run_as_root chmod 644 docker/mosquitto/config/passwords
run_as_root chmod 644 docker/mosquitto/config/mosquitto.conf 
run_as_root chmod 755 docker/mosquitto/config

print_info "Setup abgeschlossen"
printf '\nNächste Schritte:\n'
printf '  1. Neu anmelden (damit docker-Gruppe wirksam wird)\n'
printf '  2. Projekt starten: python3 scripts/run/start_project.py\n'
