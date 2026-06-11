#!/usr/bin/env bash
set -euo pipefail

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root_directory="$(cd "$script_directory/../.." && pwd)"

docker compose -f "$repository_root_directory/docker/docker-compose.yml" down
