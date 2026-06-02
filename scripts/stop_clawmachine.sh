#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

cd "$repository_root_directory"

print_info() {
    printf '\n==> %s\n' "$1"
}

print_info "Suche Python-Server-Prozess..."
python_server_pid=$(pgrep -f "python3 -m python_server.server" || true)

if [[ -n "$python_server_pid" ]]; then
    printf 'Beende Python-Server (PID: %s)...\n' "$python_server_pid"
    kill "$python_server_pid"
    sleep 1
    if kill -0 "$python_server_pid" 2>/dev/null; then
        printf 'Python-Server reagiert nicht, force kill...\n'
        kill -9 "$python_server_pid"
    fi
    printf 'Python-Server beendet.\n'
else
    printf 'Python-Server laeuft nicht.\n'
fi

print_info "Stoppe MQTT-Broker..."
docker compose -f "$repository_root_directory/docker/docker-compose.yml" down

printf '\nClawmachine gestoppt.\n'
