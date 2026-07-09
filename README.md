# Softwarepraktikum-Clawmachine

Steuerbarer Greifautomat auf Basis von ESP32-Boards, einem zentralen Python-Server und einem MQTT-Broker (Eclipse Mosquitto via Docker).

---

## Schnellstart

### Lokale Entwicklung (Entwicklungsrechner)

```bash
./scripts/setup/init_project.sh
python3 scripts/run/start_project.py
```

`init_project.sh` erkennt automatisch den Paketmanager (apt, pacman/Arch, brew) und richtet die Python-Virtualenv ein. Beim ersten Ausführen wird außerdem `config.local.ini` aus der Vorlage angelegt.

### Server-Setup (Ubuntu Server, als root)

```bash
sudo python3 scripts/setup/setup_server.py
python3 scripts/run/start_project.py
```

`setup_server.py` ist eine interaktive TUI: zeigt Systemzustand, plant die Schritte (Docker, Python-Deps, WLAN-Hotspot) und fragt vor der Ausführung nach Bestätigung.

---

## Scripts – Übersicht und Aufruf-Reihenfolge

### Alle Scripts auf einen Blick

| Script | Ort | Zweck |
|---|---|---|
| `setup_server.py` | `scripts/setup/` | **Server-Einrichtung (TUI)** – Docker, Python-Deps, Hotspot |
| `setup_server.sh` | `scripts/setup/` | Server-Einrichtung ohne TUI (legacy) |
| `setup_hotspot.py` | `scripts/setup/` | **Hotspot-Einrichtung (TUI)** – wlan0/wlo1 als Access Point |
| `init_project.sh` | `scripts/setup/` | Lokale Entwicklungsumgebung einrichten (apt/pacman/brew) |
| `install_python_dependencies.sh` | `scripts/setup/` | `.venv` erstellen + `requirements.txt` installieren |
| `install_deb_files_on_raspberry.sh` | `scripts/setup/` | `.deb`-Pakete aus `data/deb_files/` installieren (Offline) |
| `start_project.py` | `scripts/run/` | **Projekt starten (TUI)** – Broker + Server mit Live-Logs |
| `start_project.sh` | `scripts/run/` | Projekt starten ohne TUI (legacy) |
| `start_mqtt_broker.sh` | `scripts/run/` | Nur MQTT-Broker starten (Docker) |
| `stop_clawmachine.sh` | `scripts/run/` | Server + Broker stoppen |
| `stop_mqtt_broker.sh` | `scripts/run/` | Nur MQTT-Broker stoppen |
| `generate_firmware_config.py` | `scripts/dev/` | `config.ini` → `include/firmware_config.h` |
| `run_emulated_esp_once.sh` | `scripts/dev/` | Simulierten ESP32 starten (Uptime-Daten an Broker) |
| `mqtt_broker_logs.sh` | `scripts/dev/` | Broker-Logs live anzeigen |
| `mqtt_message_logs.sh` | `scripts/dev/` | Alle MQTT-Nachrichten mitschneiden (`#`) |

---

### Aufruf-Graph

```
╔══════════════════════════════════════════════════════════════════╗
║  EINMALIG – Lokale Entwicklung (Dev-PC)                         ║
╚══════════════════════════════════════════════════════════════════╝

  scripts/setup/init_project.sh
  │   - Erkennt Paketmanager (apt / pacman / brew)
  │   - Legt config.local.ini aus Vorlage an
  └──► scripts/setup/install_python_dependencies.sh
           - Erstellt .venv, installiert requirements.txt


╔══════════════════════════════════════════════════════════════════╗
║  EINMALIG – Server-Setup (Raspberry Pi, als root)               ║
╚══════════════════════════════════════════════════════════════════╝

  sudo python3 scripts/setup/setup_server.py        ← TUI ✦
  │   - Zeigt Systemzustand + geplante Schritte
  ├── apt-get update / upgrade
  ├── Docker CE + Compose installieren
  ├── python3, pip, venv, mosquitto-clients
  ├── scripts/setup/install_python_dependencies.sh
  └── scripts/setup/setup_hotspot.py                ← TUI ✦
          - Erkennt WLAN-Interface (wlan0 / wlo1)
          - Liest SSID + Passwort aus config.ini
          - Konfiguriert hostapd + dnsmasq


╔══════════════════════════════════════════════════════════════════╗
║  NACH JEDER config.ini-ÄNDERUNG (Dev-PC)                        ║
╚══════════════════════════════════════════════════════════════════╝

  python scripts/dev/generate_firmware_config.py
  │   Liest config.ini (+ config.local.ini)
  └── Schreibt include/firmware_config.h
          └──► pio run -e claw_1 / claw_3 / ...   (ESP flashen)


╔══════════════════════════════════════════════════════════════════╗
║  Projekt starten (Ubuntu Server)                       ║
╚══════════════════════════════════════════════════════════════════╝

  python3 scripts/run/start_project.py               ← TUI ✦
  ├── docker compose up -d        (MQTT-Broker)
  │       Port 1883 + 9001
  └── python -m python_server     (Server, Live-Logs)
          │
          Strg+C ──► Sauberer Shutdown


╔══════════════════════════════════════════════════════════════════╗
║  Projekt stoppen                                       ║
╚══════════════════════════════════════════════════════════════════╝

  scripts/run/stop_clawmachine.sh
  ├── pkill python_server
  └── docker compose down


╔══════════════════════════════════════════════════════════════════╗
║  ENTWICKLUNG / DEBUG (jederzeit)                                 ║
╚══════════════════════════════════════════════════════════════════╝

  scripts/dev/generate_firmware_config.py   config.ini → firmware_config.h
  scripts/dev/run_emulated_esp_once.sh      Simulierter ESP32
  scripts/dev/mqtt_broker_logs.sh           Broker-Logs
  scripts/dev/mqtt_message_logs.sh          Alle MQTT-Nachrichten live
```

> **✦ TUI-Scripts** zeigen vor der Ausführung den aktuellen Systemzustand,
> die geplanten Änderungen und fragen nach Bestätigung.
> Sie sind idempotent – mehrfach ausführbar.

---

## Topic-Struktur

| TOPIC | retained | Beschreibung |
| --- | --- | --- | 
| `clawmachine/<device_id>/announce` | ja | beim Boot gesendet|
| `clawmachine/<device_id>/status` | nein | online/offline (via LWT) |
| `clawmachine/<device_id>/telemetry` | nein | laufende Daten (z.B. Motorposition) |
| `clawmachine/<device_id>/cmd` | nein | Server → Controller (Befehle) |

---

## Konfiguration

Die Projektkonfiguration liegt in zwei Dateien im Projektroot:

| Datei | Zweck | In git? |
|---|---|---|
| `config.ini` | Standardwerte für den Server-Betrieb | ja |
| `config.local.ini` | Lokale Überschreibungen (z. B. Broker-IP) | nein (gitignored) |

`config.local.ini` überschreibt `config.ini`. Es genügt, nur die abweichenden Werte einzutragen.

### config.ini — Felder

```ini
[mqtt]
broker = 192.168.0.103       # Adresse des MQTT-Brokers
                             # Server: localhost (Broker läuft lokal)
                             # Lokal:  192.168.0.103 (Broker auf dem Pi)
port = 1883                  # MQTT-Port (Standard: 1883)
username = clawmachine       # MQTT-Benutzername
password = claw_secret       # MQTT-Passwort

# Nur Python-Server
topic = clawmachine/claw
device_added_topic = clawmachine/device/added
client_id = server
connect_timeout_seconds = 5

[wifi]
# ESP-Firmware — wird via generate_firmware_config.py in firmware_config.h geschrieben
ssid = clawmachine_server    # SSID des Raspberry Pi Hotspots
password = claw_secret       # WiFi-Passwort

[hotspot]
# Raspberry Pi Access Point — wird von setup_hotspot.py verwendet
# SSID und Passwort kommen aus [wifi]
ip = 192.168.0.103           # Feste IP des Pi im Hotspot-Netz
prefix = 24
dhcp_start = 192.168.0.100   # DHCP-Bereich für ESP32-Boards
dhcp_end = 192.168.0.200
dhcp_lease = 12h
channel = 6
```

### Lokale Entwicklung: config.local.ini anlegen

```bash
cp config.local.ini.example config.local.ini
# broker-Zeile anpassen, z. B.:
#   broker = 192.168.0.103
```

Die Datei wird von git ignoriert.

### ESP-Firmware-Konfiguration generieren

Geteilte Werte (MQTT-Broker-Adresse, WiFi-Zugangsdaten) werden aus `config.ini`/`config.local.ini` in `include/firmware_config.h` geschrieben:

```bash
python scripts/dev/generate_firmware_config.py
# danach ESP bauen:
pio run -e claw_motor_controller
```

---

## Python-Server manuell starten

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r python_server/requirements.txt
python -m python_server
```

---

## MQTT-Broker (Docker)

```bash
./scripts/run/start_mqtt_broker.sh    # starten
./scripts/run/stop_mqtt_broker.sh     # stoppen
./scripts/dev/mqtt_broker_logs.sh     # Logs anzeigen
./scripts/dev/mqtt_message_logs.sh    # alle MQTT-Nachrichten mitschneiden
```

---

## ESP32-Firmware bauen

```bash
pio run -e claw_motor_controller   # Motor-Controller
pio run -e claw_server             # Control Panel
pio run -e claw_player_input       # Joy-Con Empfänger
```

Gemeinsame compile-time Konfiguration: `include/firmware_config.h`  
Geteilte Werte aus `config.ini` generieren: `python scripts/dev/generate_firmware_config.py`

---

## Emulator (ohne Hardware testen)

```bash
./scripts/dev/run_emulated_esp_once.sh
```

Simuliert ein ESP-Gerät und sendet Uptime-Daten an den Broker.
