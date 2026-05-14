#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(dirname "$script_directory")"

docker compose -f "$repository_root_directory/docker/docker-compose.yml" logs -f mqtt-broker
