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

docker restart mqtt-broker >/dev/null
echo "Passwort für '$mqtt_username' gesetzt. Broker wurde neu gestartet, damit die Änderung greift."
