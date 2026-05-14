# Softwarepraktikum-Clawmachine

## Python server (lokal und spaeter Raspberry Pi)

Der Python-Server stellt bereit:

- TCP-Server fuer ESP32-Clients (Standard: Port `3333`)
- HTTP-API und Dashboard (Standard: Port `8080`)
- Multi-Client-Verwaltung mit Heartbeats

### Starten

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r python_server/requirements.txt
python -m python_server
```

Dann im Browser: `http://127.0.0.1:8080`

### Konfiguration per Umgebungsvariablen

- `CLAW_TCP_HOST` (default: `0.0.0.0`)
- `CLAW_TCP_PORT` (default: `3333`)
- `CLAW_HTTP_HOST` (default: `0.0.0.0`)
- `CLAW_HTTP_PORT` (default: `8080`)
- `CLAW_HEARTBEAT_SECONDS` (default: `5`)
- `CLAW_MAX_CLIENTS` (default: `8`)

Beispiel:

```bash
CLAW_TCP_PORT=3333 CLAW_HTTP_PORT=8080 CLAW_MAX_CLIENTS=8 python -m python_server
```

### MQTT-Konfiguration fuer den Python-Server

Die MQTT-Verbindung wird in `python_server/config.ini` konfiguriert:

```ini
[mqtt]
broker = localhost
port = 1883
topic = clawmachine/claw
client_id = server
connect_timeout_seconds = 5
```

### Firmware-Konfiguration fuer ESP32

Gemeinsame compile-time Konfiguration liegt in `include/firmware_config.h`.
Diese Werte werden beim Build in den C++-Code uebernommen und koennen dort zentral angepasst werden.
