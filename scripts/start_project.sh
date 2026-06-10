#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"
venv_directory_path="$repository_root_directory/.venv"
docker_compose_file_path="$repository_root_directory/docker/docker-compose.yml"
requirements_file_path="$repository_root_directory/python_server/requirements.txt"

cd "$repository_root_directory"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_error() {
    printf 'Fehler: %s\n' "$1" >&2
}

if ! command -v docker >/dev/null 2>&1; then
    print_error "docker ist nicht installiert. Bitte zuerst ./scripts/init_project.sh ausfuehren."
    exit 1
fi

if ! docker compose version >/dev/null 2>&1; then
    print_error "docker compose ist nicht verfuegbar. Bitte zuerst ./scripts/init_project.sh ausfuehren."
    exit 1
fi

if [[ ! -f "$docker_compose_file_path" ]]; then
    print_error "docker-compose Datei nicht gefunden: $docker_compose_file_path"
    exit 1
fi

if [[ ! -f "$venv_directory_path/bin/activate" ]]; then
    print_error "Virtualenv nicht gefunden unter $venv_directory_path"
    print_error "Bitte zuerst ./scripts/init_project.sh ausfuehren."
    exit 1
fi

if [[ ! -f "$requirements_file_path" ]]; then
    print_error "requirements.txt nicht gefunden: $requirements_file_path"
    exit 1
fi

print_info "Starte Docker-Container"
docker compose -f "$docker_compose_file_path" up -d

print_info "Starte Python-Server"
# shellcheck disable=SC1091
source "$venv_directory_path/bin/activate"

if ! python -c "from paho.mqtt import client" >/dev/null 2>&1; then
    print_info "Installiere fehlende Python-Abhaengigkeiten"
    python -m pip install -r "$requirements_file_path"
fi

printf '(Strg+C beendet den Python-Server. Der Docker-Container laeuft weiter.)\n\n'
python -m python_server.server
