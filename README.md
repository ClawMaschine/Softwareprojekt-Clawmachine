# Softwarepraktikum-Clawmachine

Steuerbarer Greifautomat auf Basis von ESP32-Boards, einem zentralen Python-Server und einem MQTT-Broker (Eclipse Mosquitto via Docker).

---

## Schnellstart

### Lokale Entwicklung (Entwicklungsrechner)

```bash
./scripts/setup/init_project.sh
./scripts/run/start_project.sh
```

`init_project.sh` erkennt automatisch den Paketmanager (apt, pacman/Arch, brew) und richtet die Python-Virtualenv ein. Beim ersten Ausführen wird außerdem `config.local.ini` aus der Vorlage angelegt.

### Server-Setup (Ubuntu x64)

```bash
./scripts/setup/setup_server.sh
./scripts/run/start_project.sh
```

`setup_server.sh` installiert Docker, Python und alle Abhängigkeiten.

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
broker = localhost           # Adresse des MQTT-Brokers
                             # Server: localhost (Broker läuft lokal)
                             # Lokal:  192.168.0.103 (Broker auf dem Server)
port = 1883                  # MQTT-Port (Standard: 1883)
username = clawmachine       # MQTT-Benutzername
password = claw_secret       # MQTT-Passwort

# Nur Python-Server
topic = clawmachine/claw
device_added_topic = clawmachine/device/added
client_id = server
connect_timeout_seconds = 5

[wifi]
# Nur ESP-Firmware (via generate_firmware_config.py)
ssid = praktikum
password =                   # WiFi-Passwort hier eintragen
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
