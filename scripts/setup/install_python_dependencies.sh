#!/usr/bin/env bash
# Erstellt die Python-Virtualenv und installiert alle Abhängigkeiten.
# Plattformunabhängig — keine Systempaket-Installation.
# Kann allein oder von anderen Setup-Skripten aufgerufen werden.
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"
venv_directory_path="$repository_root_directory/.venv"
requirements_file_path="$repository_root_directory/python_server/requirements.txt"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_error() {
    printf 'Fehler: %s\n' "$1" >&2
}

if ! command -v python3 >/dev/null 2>&1; then
    print_error "python3 ist nicht installiert."
    exit 1
fi

if ! python3 -m venv --help >/dev/null 2>&1; then
    print_error "python3-venv ist nicht installiert."
    exit 1
fi

if [[ ! -f "$requirements_file_path" ]]; then
    print_error "requirements.txt nicht gefunden: $requirements_file_path"
    exit 1
fi

if [[ ! -d "$venv_directory_path" ]]; then
    print_info "Erstelle virtuelle Python-Umgebung"
    python3 -m venv "$venv_directory_path"
else
    print_info "Virtuelle Python-Umgebung bereits vorhanden"
fi

print_info "Installiere Python-Abhängigkeiten"
# shellcheck disable=SC1091
source "$venv_directory_path/bin/activate"
python -m pip install --upgrade pip --quiet
python -m pip install -r "$requirements_file_path"

printf '\nPython-Abhängigkeiten installiert in: %s\n' "$venv_directory_path"
