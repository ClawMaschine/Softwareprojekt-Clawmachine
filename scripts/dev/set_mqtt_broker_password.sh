#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Verwendung: $0 <benutzername>" >&2
  exit 1
fi

mqtt_username="$1"

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"
compose_file_path="$repository_root_directory/docker/docker-compose.yml"
mosquitto_config_directory="$repository_root_directory/docker/mosquitto/config"
mosquitto_passwords_path="$mosquitto_config_directory/passwords"

if [[ ! -f "$mosquitto_passwords_path" ]]; then
  # mosquitto.conf verlangt password_file schon beim Start (allow_anonymous
  # false) — der Broker-Container kommt nie hoch, solange die Datei fehlt,
  # und haengt sonst in einer Restart-Schleife. mosquitto_passwd kann eine
  # fehlende Datei nur mit -c neu anlegen, nicht per "docker exec" in einem
  # Broker, der deswegen gar nicht erst laeuft — deshalb hier ueber einen
  # eigenstaendigen Einmal-Container, genau wie beim Bootstrap in
  # setup_server.sh.
  echo "Keine Passwortdatei gefunden – lege sie neu an..."
  docker run --rm -it \
    -v "$mosquitto_config_directory:/mosquitto/config" \
    eclipse-mosquitto:latest \
    mosquitto_passwd -c /mosquitto/config/passwords "$mqtt_username"
  chmod 644 "$mosquitto_passwords_path" 2>/dev/null || true
else
  if ! docker ps --format '{{.Names}}' | grep -qx "mqtt-broker"; then
    echo "Container 'mqtt-broker' läuft nicht – starte ihn..."
    docker compose -f "$compose_file_path" up -d mqtt-broker

    for _ in $(seq 1 10); do
      docker ps --format '{{.Names}}' | grep -qx "mqtt-broker" && break
      sleep 0.5
    done

    if ! docker ps --format '{{.Names}}' | grep -qx "mqtt-broker"; then
      echo "Fehler: Container 'mqtt-broker' konnte nicht gestartet werden." >&2
      exit 1
    fi
  fi

  docker exec -it mqtt-broker mosquitto_passwd /mosquitto/config/passwords "$mqtt_username"
fi

docker compose -f "$compose_file_path" up -d mqtt-broker >/dev/null
docker restart mqtt-broker >/dev/null
echo "Passwort für '$mqtt_username' gesetzt. Broker wurde neu gestartet, damit die Änderung greift."
