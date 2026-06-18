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

print_info "Setup abgeschlossen"
printf '\nNächste Schritte:\n'
printf '  1. Neu anmelden (damit docker-Gruppe wirksam wird)\n'
printf '  2. MQTT-Broker starten: ./scripts/run/start_mqtt_broker.sh\n'
printf '  3. Server starten:      ./scripts/run/start_project.sh\n'
