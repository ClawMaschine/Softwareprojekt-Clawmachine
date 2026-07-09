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
            run_as_root apt-get install -y "${packages[@]}"
            ;;
        pacman)
            # Paketnamen für Arch/Manjaro anpassen
            local arch_packages=()
            for pkg in "${packages[@]}"; do
                case "$pkg" in
                    python3-pip|python3-venv) arch_packages+=("python") ;;
                    mosquitto-clients)        arch_packages+=("mosquitto") ;;
                    *)                        arch_packages+=("$pkg") ;;
                esac
            done
            # Deduplizieren
            local unique_packages
            mapfile -t unique_packages < <(printf '%s\n' "${arch_packages[@]}" | sort -u)
            run_as_root pacman -S --needed --noconfirm "${unique_packages[@]}"
            ;;
        brew)
            brew install "${packages[@]}"
            ;;
        none)
            print_error "Kein bekannter Paketmanager gefunden. Bitte manuell installieren: ${packages[*]}"
            exit 1
            ;;
    esac
}



install_bluepad32_components() {
    local components_directory="$repository_root_directory/local_components"
    local bluepad_template_url="https://github.com/ricardoquesada/esp-idf-arduino-bluepad32-template.git"
    local temp_directory

    print_info "Bluepad32-Komponenten prüfen"

    mkdir -p "$components_directory"

    if [[ -d "$components_directory/bluepad32" ]]; then
        printf 'Bluepad32-Komponente bereits vorhanden: %s\n' "$components_directory/bluepad32"
    else
        print_info "Bluepad32-Komponenten herunterladen"

        temp_directory="$(mktemp -d)"

        git clone --recursive "$bluepad_template_url" "$temp_directory/bluepad32_template"

        for component_name in bluepad32 bluepad32_arduino btstack arduino; do
            if [[ -d "$components_directory/$component_name" ]]; then
                printf 'Komponente bereits vorhanden: %s\n' "$component_name"
                continue
            fi

            if [[ ! -d "$temp_directory/bluepad32_template/components/$component_name" ]]; then
                print_error "Komponente im Template nicht gefunden: $component_name"
                rm -rf "$temp_directory"
                exit 1
            fi

            cp -r "$temp_directory/bluepad32_template/components/$component_name" "$components_directory/"
            printf 'Komponente hinzugefügt: %s\n' "$component_name"
        done

        rm -rf "$temp_directory"
    fi
}

install_esp_idf_console_components() {
    local components_directory="$repository_root_directory/local_components"
    local esp_idf_directory="${IDF_PATH:-$HOME/.platformio/packages/framework-espidf}"
    local source_base_directory="$esp_idf_directory/examples/system/console/advanced/components"

    print_info "ESP-IDF-Console-Komponenten prüfen"

    mkdir -p "$components_directory"

    for component_name in cmd_nvs cmd_system cmd_wifi; do
        if [[ -d "$components_directory/$component_name" ]]; then
            printf 'Komponente bereits vorhanden: %s\n' "$component_name"
            continue
        fi

        if [[ ! -d "$source_base_directory/$component_name" ]]; then
            print_warning "Komponente nicht gefunden: $source_base_directory/$component_name"
            continue
        fi

        cp -r "$source_base_directory/$component_name" "$components_directory/"
        printf 'Komponente hinzugefügt: %s\n' "$component_name"
    done
}

patch_bluepad32_for_platformio() {
    print_info "Bluepad32 PlatformIO-Kompatibilität prüfen"

    local files_to_patch=(
        "$repository_root_directory/local_components/bluepad32/platform/uni_platform_unijoysticle.c"
        "$repository_root_directory/local_components/bluepad32_arduino/arduino_platform.c"
    )

    for file_path in "${files_to_patch[@]}"; do
        if [[ ! -f "$file_path" ]]; then
            print_warning "Bluepad32-Datei nicht gefunden: $file_path"
            continue
        fi

        sed -i 's/[[:space:]]*cmd_system_version();/    \/\/ cmd_system_version();/' "$file_path"
        printf 'Bluepad32 cmd_system_version-Patch angewendet: %s\n' "$file_path"
    done
}

print_info "Lokale Entwicklungsumgebung einrichten"

# Voraussetzungen prüfen
has_python3=false
has_venv_module=false
pkg_manager="$(detect_package_manager)"
missing_packages=()

if command -v python3 >/dev/null 2>&1; then
    has_python3=true
fi

if $has_python3 && python3 -m venv --help >/dev/null 2>&1; then
    has_venv_module=true
fi

if ! $has_python3; then
    missing_packages+=("python3")
fi

if ! $has_venv_module; then
    missing_packages+=("python3-venv")
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

# Python-Abhängigkeiten installieren
print_info "Installiere Python-Abhängigkeiten"
"$script_directory/install_python_dependencies.sh"

print_info "Fixiere kompatible Click-Version für PlatformIO/esptool"
if command -v /usr/bin/python3 >/dev/null 2>&1; then
    /usr/bin/python3 -m pip install --user --force-reinstall "click==8.1.8"
else
    python3 -m pip install --user --force-reinstall "click==8.1.8"
fi

install_bluepad32_components
install_esp_idf_console_components
patch_bluepad32_for_platformio

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
