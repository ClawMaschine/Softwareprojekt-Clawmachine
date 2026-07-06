#!/usr/bin/env bash
# Lokale Entwicklungsumgebung einrichten.
# Für Server-Setup: scripts/setup/setup_server.sh

set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"

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

# Paketmanager erkennen
detect_package_manager() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "apt"
    elif command -v pacman >/dev/null 2>&1; then
        echo "pacman"
    elif command -v brew >/dev/null 2>&1; then
        echo "brew"
    else
        echo "none"
    fi
}

install_system_packages() {
    local pkg_manager="$1"
    shift
    local packages=("$@")

    case "$pkg_manager" in
        apt)
            run_as_root apt-get update
            run_as_root apt-get install -y "${packages[@]}"
            ;;
        pacman)
            local arch_packages=()

            for pkg in "${packages[@]}"; do
                case "$pkg" in
                    python3.10)       arch_packages+=("python") ;;
                    python3.10-venv)  arch_packages+=("python") ;;
                    mosquitto-clients) arch_packages+=("mosquitto") ;;
                    *)                arch_packages+=("$pkg") ;;
                esac
            done

            mapfile -t arch_packages < <(printf '%s\n' "${arch_packages[@]}" | sort -u)

            run_as_root pacman -S --needed --noconfirm "${arch_packages[@]}"
            ;;
        brew)
            brew install "${packages[@]}"
            ;;
        none)
            print_error "Kein bekannter Paketmanager gefunden."
            print_error "Bitte manuell installieren: ${packages[*]}"
            exit 1
            ;;
    esac
}

print_info "Lokale Entwicklungsumgebung einrichten"

# Voraussetzungen prüfen
has_python310=false
has_venv_module=false
pkg_manager="$(detect_package_manager)"
missing_packages=()

if command -v python3.10 >/dev/null 2>&1; then
    has_python310=true
fi

if $has_python310 && python3.10 -m venv --help >/dev/null 2>&1; then
    has_venv_module=true
fi

if ! $has_python310; then
    missing_packages+=("python3.10")
fi

if ! $has_venv_module; then
    missing_packages+=("python3.10-venv")
fi

if [[ "${#missing_packages[@]}" -eq 0 ]]; then
    printf 'Alle Voraussetzungen vorhanden.\n'
else
    print_warning "Fehlende Pakete: ${missing_packages[*]}"
fi

if ! ask_yes_no "Entwicklungsumgebung einrichten?" "N"; then
    printf 'Abgebrochen.\n'
    exit 0
fi

# Fehlende Systempakete installieren
if [[ "${#missing_packages[@]}" -gt 0 ]]; then
    print_info "Installiere Systempakete (Paketmanager: $pkg_manager)"
    install_system_packages "$pkg_manager" "${missing_packages[@]}"
fi

# Nach Installation erneut prüfen
if ! command -v python3.10 >/dev/null 2>&1; then
    print_error "Python 3.10 konnte nicht gefunden werden."
    exit 1
fi

# Python-Abhängigkeiten installieren
print_info "Installiere Python-Abhängigkeiten"
PYTHON_BIN=python3.10 \
    "$script_directory/install_python_dependencies.sh"

# Lokale Konfiguration anlegen
local_config_path="$repository_root_directory/config.local.ini"
local_config_example_path="$repository_root_directory/config.local.ini.example"

if [[ ! -f "$local_config_path" ]]; then
    print_info "Lokale Konfiguration anlegen"
    cp "$local_config_example_path" "$local_config_path"
    printf 'config.local.ini angelegt.\n'
    printf 'Bitte Broker-Adresse anpassen falls nötig: %s\n' "$local_config_path"
else
    print_info "Lokale Konfiguration bereits vorhanden"
fi

print_info "Einrichtung abgeschlossen"

printf '\nNächste Schritte:\n'
printf '  1. config.local.ini prüfen (mqtt.broker = IP des Servers)\n'
printf '  2. MQTT-Broker lokal starten (optional): ./scripts/run/start_mqtt_broker.sh\n'
printf '  3. Server starten: ./scripts/run/start_project.sh\n'
printf '\nFür Server-Setup: ./scripts/setup/setup_server.sh\n'