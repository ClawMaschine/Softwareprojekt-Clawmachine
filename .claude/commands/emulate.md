Starte den emulierten ESP, der Uptime-Daten an den Broker schickt.

Führe aus: `python3 -m python_server.emulated_esp.emulated`

Der emulierte ESP verbindet sich als `emulated_esp` und publiziert alle 1s die Uptime auf `clawmachine/emulated_esp/metadata/uptime`.
