#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

venv_directory_path="$repository_root_directory/.venv"
requirements_file_path="$repository_root_directory/python_server/requirements.txt"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_warning() {
    printf 'Warnung: %s\n' "$1"
}

print_error() {
    printf 'Fehler: %s\n' "$1" >&2
}

ask_yes_no() {
    local question_text="$1"
    local default_answer="${2:-N}"
    local answer

    read -r -p "$question_text [$default_answer] " answer
    answer="${answer:-$default_answer}"

    case "$answer" in
        y|Y|yes|YES|j|J|ja|JA)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

run_as_root() {
    if [[ "$EUID" -eq 0 ]]; then
        "$@"
    else
        sudo "$@"
    fi
}

has_python3=false
has_pip=false
has_venv_module=false
has_docker=false
has_docker_compose=false
has_apt=false

if command -v python3 >/dev/null 2>&1; then
    has_python3=true
fi

if $has_python3 && python3 -m pip --version >/dev/null 2>&1; then
    has_pip=true
fi

if $has_python3 && python3 -m venv --help >/dev/null 2>&1; then
    has_venv_module=true
fi

if command -v docker >/dev/null 2>&1; then
    has_docker=true
fi

if $has_docker && docker compose version >/dev/null 2>&1; then
    has_docker_compose=true
fi

if command -v apt-get >/dev/null 2>&1; then
    has_apt=true
fi

print_info "Pruefe Voraussetzungen"

missing_requirements=()

if ! $has_python3; then
    missing_requirements+=("python3")
fi

if ! $has_pip; then
    missing_requirements+=("python3-pip")
fi

if ! $has_venv_module; then
    missing_requirements+=("python3-venv")
fi

if ! $has_docker; then
    missing_requirements+=("docker")
fi

if [[ "${#missing_requirements[@]}" -eq 0 ]]; then
    printf 'Alle Voraussetzungen sind vorhanden.\n'
else
    print_warning "Folgende Voraussetzungen fehlen: ${missing_requirements[*]}"
fi

if ! ask_yes_no "Projekt initialisieren (Pakete installieren, venv erstellen, Python-Abhaengigkeiten installieren)?" "N"; then
    printf 'Abgebrochen. Es wurden keine Aenderungen vorgenommen.\n'
    exit 0
fi

if [[ "${#missing_requirements[@]}" -gt 0 ]]; then
    if ! $has_apt; then
        print_error "apt-get ist nicht verfuegbar. Bitte installiere die fehlenden Pakete manuell: ${missing_requirements[*]}"
        exit 1
    fi

    run_as_root "# Add Docker's official GPG key:
sudo apt update
sudo apt install ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/ubuntu/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

# Add the repository to Apt sources:
sudo tee /etc/apt/sources.list.d/docker.sources <<EOF
Types: deb
URIs: https://download.docker.com/linux/ubuntu
Suites: $(. /etc/os-release && echo "${UBUNTU_CODENAME:-$VERSION_CODENAME}")
Components: stable
Architectures: $(dpkg --print-architecture)
Signed-By: /etc/apt/keyrings/docker.asc
EOF

sudo apt update"

    print_info "Installiere Systempakete"
    run_as_root apt-get update
    run_as_root apt-get install -y  python3 python3-pip python3-venv mosquitto-clients

    if [[ "$EUID" -ne 0 ]]; then
        run_as_root usermod -aG docker "$USER" || true
        printf 'Hinweis: Fuer Docker ohne sudo evtl. neu anmelden.\n'
    fi
fi

if [[ ! -d "$venv_directory_path" ]]; then
    print_info "Erstelle virtuelle Python-Umgebung"
    python3 -m venv "$venv_directory_path"
else
    print_info "Virtuelle Python-Umgebung bereits vorhanden"
fi

if [[ ! -f "$requirements_file_path" ]]; then
    print_error "requirements.txt nicht gefunden: $requirements_file_path"
    exit 1
fi

print_info "Installiere Python-Abhaengigkeiten"
# shellcheck disable=SC1091
source "$venv_directory_path/bin/activate"
python -m pip install --upgrade pip
python -m pip install -r "$requirements_file_path"

print_info "Initialisierung abgeschlossen"
printf 'Naechster Schritt: ./scripts/start_project.sh\n'
