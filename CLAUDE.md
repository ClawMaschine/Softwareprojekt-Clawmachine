# CLAUDE.md – Projektkontext Clawmachine

## Was ist dieses Projekt?

Ein steuerbarer **Greifautomat (Claw Machine)** als Softwarepraktikum-Projekt. Spieler steuern die Maschine mit einem Nintendo Switch Joy-Con. Die Steuerlogik ist als **Microservice-Architektur** auf mehreren ESP32-Boards und einem zentralen Raspberry Pi aufgeteilt, die über MQTT miteinander kommunizieren.

---

## Systemarchitektur

```
[Nintendo Switch Joy-Con]
         |  Classic Bluetooth
         v
[ESP32: claw_player_input]
         |  MQTT publish → clawmachine/joycon
         v
[MQTT-Broker: Eclipse Mosquitto]  ←→  [Python-Server: ClawMachine]
         |                                        |
         |  MQTT subscribe                MQTT publish → Motorbefehle
         v                                        v
[ESP32: claw_1]   [ESP32: claw_2*]   [ESP32: claw_3]   [ESP32: claw_server]
(Motor-Achse 1)   (Motor-Achse 2)   (Motor-Achse 3)   (Steuereinheit)
```

`*` claw_2 ist noch nicht implementiert.

---

## Geplanter End-to-End-Datenfluss

1. Spieler drückt Taste am **Nintendo Switch Joy-Con**
2. **`claw_player_input`** ESP32 empfängt das Signal via Classic Bluetooth
3. ESP32 publiziert Steuerbefehl auf MQTT-Topic `clawmachine/joycon`
4. **Python-Server** (auf Raspberry Pi) empfängt den Befehl, interpretiert ihn und publiziert Motorbefehle
5. **Motor-ESPs** (`claw_1`, `claw_2`, `claw_3`) empfangen die Motorbefehle und steuern die physischen Motoren der Maschine

---

## Infrastruktur

| Komponente        | Ort                         | Details                              |
|-------------------|-----------------------------|--------------------------------------|
| MQTT-Broker       | Raspberry Pi (192.168.0.103) | Eclipse Mosquitto via Docker, Port 1883 |
| Python-Server     | Raspberry Pi                | FastAPI + paho-mqtt, `python_server/` |
| ESP32-Boards      | Claw Machine Hardware       | PlatformIO / Arduino Framework       |
| WiFi-Netzwerk     | SSID: `praktikum`           | Passwort in `firmware_config.h`      |

---

## ESP32-Boards und ihre Rollen

| Board-Name          | PlatformIO env       | Quellordner              | Rolle                                                  |
|---------------------|----------------------|--------------------------|--------------------------------------------------------|
| `claw_player_input` | `claw_player_input`  | `src_claw_player_input/` | Empfängt Joy-Con via Bluetooth Classic, sendet an MQTT |
| `claw_1`            | `claw_1`             | `src_claw_1/`            | Motor-Controller Achse 1                               |
| `claw_2`            | *(noch nicht impl.)* | *(noch nicht erstellt)*  | Motor-Controller Achse 2 (geplant)                     |
| `claw_3`            | `claw_3`             | `src_claw_3/`            | Motor-Controller Achse 3                               |
| `claw_server`       | `claw_server`        | `src_claw_server/`       | Steuereinheit / Control Panel                          |

---

## MQTT-Topics

| Topic                        | Richtung              | Beschreibung                                 |
|------------------------------|-----------------------|----------------------------------------------|
| `clawmachine/joycon`         | ESP32 → Broker        | Spieler-Steuerbefehle (`open`, `close`)      |
| `clawmachine/claw`           | Server → Broker       | Haupttopic des Servers                       |
| `clawmachine/device/added`   | ESP32 → Broker        | Registrierung neuer ESP32-Geräte             |
| `claw/uptime_milliseconds`   | ESP32 → Broker        | Heartbeat/Uptime der ESP32-Boards            |
| `claw/uptime_seconds`        | ESP32 → Broker        | Heartbeat/Uptime der ESP32-Boards            |
| `claw/uptime_minutes`        | ESP32 → Broker        | Heartbeat/Uptime der ESP32-Boards            |

---

## Gemeinsamer Code

- **`lib/claw_mqtt_connection/`** – `ClawMqttConnection`-Klasse: WiFi + MQTT-Verbindungsmanagement für alle ESP32-Boards. Wird von allen `src_claw_*`-Firmwares verwendet.
- **`include/firmware_config.h`** – Gemeinsame compile-time Konfiguration: WiFi-SSID/Passwort, MQTT-Broker-IP, Bluetooth-MACs der Joy-Cons.

---

## Bekannte Hardware-Konfiguration (firmware_config.h)

| Gerät                 | MAC-Adresse         | Client-ID           |
|-----------------------|---------------------|---------------------|
| Joy-Con (R)           | E0:F6:B5:53:BA:93   | Joy-Con (R)         |
| Joy-Con (L)           | E0:F6:B5:4F:A8:B4   | Joy-Con (L)         |
| Pro Controller        | E0:F6:B5:66:96:7E   | Pro Controller      |
| MQTT-Broker           | 192.168.0.103       | Port 1883           |

---

## Python-Server starten

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r python_server/requirements.txt
python -m python_server
```

Konfiguration: `python_server/config.ini`

---

## PlatformIO Build-Regeln

**Vor jeder Änderung: `platformio.ini` lesen.**

Jedes Board hat ein eigenes `[env:...]`. Source-Ordner werden über `build_src_filter` gesteuert — nicht über `src_dir`.

```bash
pio run -e claw_1               # Nur claw_1 bauen
pio run -e claw_player_input    # Nur claw_player_input bauen
pio run -e claw_server          # Nur claw_server bauen
```

Wenn gemeinsamer Code (`lib/` oder `include/`) geändert wird: alle betroffenen Environments bauen.

---

## Projektstruktur

```
platformio.ini              ← Build-Konfiguration für alle ESP32-Boards
include/firmware_config.h   ← Gemeinsame Konstanten (WiFi, MQTT, Bluetooth-MACs)
lib/claw_mqtt_connection/   ← Gemeinsame WiFi+MQTT-Library für alle ESP32s
src_claw_1/                 ← Firmware Motor-Achse 1
src_claw_3/                 ← Firmware Motor-Achse 3
src_claw_player_input/      ← Firmware Joy-Con Bluetooth-Empfänger
src_claw_server/            ← Firmware Control Panel
python_server/              ← Zentrale Steuerlogik auf Raspberry Pi (FastAPI + MQTT)
docker/                     ← Docker Compose für MQTT-Broker (Mosquitto)
scripts/setup/              ← Einmalige Einrichtung (init_project, setup_server, Python-Deps)
scripts/run/                ← Tägliche Nutzung (start/stop Projekt und MQTT-Broker)
scripts/dev/                ← Entwicklung/Debugging (Logs, ESP-Emulator, firmware_config generieren)
```

---

## Namens- und Codierungsregeln

- **Lesbarkeit vor Kürze** – keine kryptischen Abkürzungen
- Boolesche Variablen mit Präfix: `is`, `has`, `should`, `can`
- Pin-Namen vollständig: `clawOpenLimitSwitchPin` statt `sw1Pin`
- Funktionen mit Verb: `initializeWifiConnection()`, `readClawPosition()`
- Gemeinsamer Code gehört in `lib/` oder `include/`, nicht per Copy-Paste in mehrere `src_*`
- Kein Board-spezifischer Code (feste Pins, Hostnamen, Rollenlogik) in `lib/`
- Keine Ordnerstruktur ohne ausdrücklichen Auftrag umbauen
- Commits benennen, welches Board und welcher `src_*`-Ordner betroffen ist

---

## Was vermieden werden soll

- Unlesbare Variablennamen
- Copy-Paste-Logik zwischen `src_*`-Ordnern
- `src_dir` pro Environment setzen (funktioniert in PlatformIO nicht)
- Harte Annahmen über genau ein Board in gemeinsamen Libraries
- Ungefragtes Umbenennen von Environments oder Ordnern
