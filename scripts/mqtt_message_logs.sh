#!/usr/bin/env bash
set -euo pipefail

docker exec -i mqtt-broker mosquitto_sub -h localhost -p 1883 -t "#" -v
