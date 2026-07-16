#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Verwendung: $0 <benutzername>" >&2
  exit 1
fi

mqtt_username="$1"

if ! docker ps --format '{{.Names}}' | grep -qx "mqtt-broker"; then
  echo "Fehler: Container 'mqtt-broker' läuft nicht. Erst mit scripts/run/start_mqtt_broker.sh starten." >&2
  exit 1
fi

docker exec -it mqtt-broker mosquitto_passwd /mosquitto/config/passwords "$mqtt_username"

docker restart mqtt-broker >/dev/null
echo "Passwort für '$mqtt_username' gesetzt. Broker wurde neu gestartet, damit die Änderung greift."
