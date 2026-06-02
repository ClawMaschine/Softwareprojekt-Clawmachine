#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

cd "$repository_root_directory"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_info "Starte MQTT-Broker..."
docker compose -f "$repository_root_directory/docker/docker-compose.yml" up -d

print_info "Aktiviere Python-Virtualenv..."
venv_path="$repository_root_directory/.venv"

if [[ ! -f "$venv_path/bin/activate" ]]; then
    printf 'Fehler: Virtualenv nicht gefunden unter %s\n' "$venv_path" >&2
    printf 'Bitte zuerst ./scripts/setup_raspberrypi.sh ausfuehren.\n' >&2
    exit 1
fi

# shellcheck disable=SC1091
source "$venv_path/bin/activate"

print_info "Starte Python-Server..."
printf '(Strg+C beendet den Python-Server; der MQTT-Broker laeuft weiter.)\n\n'
python3 -m python_server.server
