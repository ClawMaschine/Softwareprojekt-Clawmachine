#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

cd "$repository_root_directory"

if [[ -z "${VIRTUAL_ENV:-}" ]]; then
    if [[ -f "$repository_root_directory/.venv/bin/activate" ]]; then
        # shellcheck disable=SC1091
        source "$repository_root_directory/.venv/bin/activate"
    elif [[ -f "$repository_root_directory/venv/bin/activate" ]]; then
        # shellcheck disable=SC1091
        source "$repository_root_directory/venv/bin/activate"
    elif [[ -f "$repository_root_directory/python_server/.venv/bin/activate" ]]; then
        # shellcheck disable=SC1091
        source "$repository_root_directory/python_server/.venv/bin/activate"
    elif [[ -f "$repository_root_directory/python_server/venv/bin/activate" ]]; then
        # shellcheck disable=SC1091
        source "$repository_root_directory/python_server/venv/bin/activate"
    else
        printf 'Keine virtuelle Umgebung gefunden.\n'
        printf 'Erwartet z. B. .venv/bin/activate oder venv/bin/activate im Repo.\n'
        exit 1
    fi
fi

if ! python3 -c "import paho.mqtt.client" >/dev/null 2>&1; then
    printf 'Fehlende Abhaengigkeit: paho-mqtt.\nInstalliere mit: python3 -m pip install -r python_server/requirements.txt\n'
    exit 1
fi

python3 -m python_server.emulated_esp.emulated
