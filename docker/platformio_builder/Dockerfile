# Build-Umgebung fuer die ESP32-Firmware (PlatformIO), genutzt von
# scripts/dev/ota_update.py — damit auf dem Server kein PlatformIO installiert
# sein muss. Wird beim ersten Aufruf automatisch gebaut.
FROM python:3.11-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends git \
    && rm -rf /var/lib/apt/lists/* \
    && pip install --no-cache-dir platformio

# Platform-/Toolchain-Cache (mehrere hundert MB): wird als benanntes Volume
# gemountet, damit nicht bei jedem Build neu heruntergeladen wird. Fuer jeden
# Benutzer beschreibbar, da der Container mit der UID des Aufrufers laeuft.
ENV PLATFORMIO_CORE_DIR=/pio-cache
RUN mkdir -p /pio-cache && chmod 777 /pio-cache

WORKDIR /workspace
