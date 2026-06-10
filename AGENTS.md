# AGENTS.md

## Projektüberblick
Dieses Repository enthält ein PlatformIO-Projekt mit mehreren ESP32-Boards.

Jedes Board bzw. jede Firmware-Variante liegt in einem eigenen Quellordner, zum Beispiel:
- `./src_server`
- `./src_claw1`
- `./src_claw2`
- weitere `./src_*`-Ordner

Jeder `[env:...]`-Eintrag in `platformio.ini` baut genau den zugehörigen Source-Ordner. Für dieses Muster ist `build_src_filter` der richtige Weg; `src_dir` ist in PlatformIO keine env-spezifische Option. [web:29][web:33][web:35]

---

## Wichtige Regeln
- Immer zuerst `platformio.ini` lesen, bevor Code geändert wird. [web:29][web:35]
- Niemals annehmen, dass es nur ein einziges ESP32-Target gibt.
- Änderungen nur im passenden `src_*`-Ordner oder in bewusst gemeinsam genutztem Code vornehmen.
- Keine Ordnerstruktur ohne ausdrücklichen Auftrag umbauen.
- Lesbare, sprechende Variablennamen verwenden.
- Keine unnötigen Abkürzungen in Variablen-, Funktions- oder Dateinamen verwenden. Lesbare und aussagekräftige Namen sind ein sinnvoller Bestandteil projektspezifischer Agent-Regeln. [web:2][web:36]

---

## Namensregeln
Lesbarkeit ist wichtiger als Kürze.

### Variablen
Bevorzugt:
- `serverConnectionState`
- `clawMotorSpeed`
- `targetPosition`
- `lastHeartbeatMillis`
- `isEmergencyStopActive`

Vermeiden:
- `srvSt`
- `cms`
- `tp`
- `lhm`
- `flag1`

Regeln:
- Variablen müssen ihren Zweck erkennen lassen.
- Boolesche Variablen mit klaren Präfixen wie `is`, `has`, `should`, `can` benennen.
- Keine Ein-Buchstaben-Variablen außer bei sehr lokalen Schleifen wie `i`.
- Keine kryptischen Hardware-Abkürzungen, wenn ein klarer Name möglich ist.
- Pin-Namen ebenfalls lesbar halten, z. B. `clawOpenLimitSwitchPin` statt `sw1Pin`.

### Funktionen
Bevorzugt:
- `initializeWifiConnection()`
- `readClawPosition()`
- `sendStatusUpdate()`
- `handleEmergencyStop()`

Vermeiden:
- `doWifi()`
- `procData()`
- `handleIt()`

### Konstanten und Makros
- Konstanten klar benennen, z. B. `kStatusSendIntervalMs` oder `STATUS_SEND_INTERVAL_MS`
- Makros nur verwenden, wenn sie wirklich nötig sind
- Board-spezifische Defines ebenfalls sprechend benennen, z. B. `BOARD_ROLE_SERVER` statt `ROLE1`

---

## Projektstruktur
Erwartetes Muster:
- `platformio.ini`
- `src_server/`
- `src_claw1/`
- `src_claw2/`
- weitere `src_*`-Ordner für weitere ESP32-Firmwares
- `include/` für gemeinsame Header
- `lib/` für gemeinsam genutzte Module

Gemeinsame Logik gehört bevorzugt in `lib/` oder `include/`, nicht durch Copy-Paste in mehrere `src_*`-Ordner. PlatformIO-Setups mit mehreren unabhängigen Source-Ordnern werden typischerweise über env-spezifische Filter mit gemeinsam nutzbaren Libraries organisiert. [web:25][web:26][web:37]

---

## platformio.ini-Regeln
- Jedes ESP32-Board bekommt ein eigenes `[env:...]`.
- Jedes Environment darf nur seinen eigenen `src_*`-Ordner bauen.
- Dafür `build_src_filter` verwenden. Diese Option ist genau dafür gedacht, gezielt Quellen aus dem Projekt für einen Build ein- oder auszuschließen. [web:29]
- `src_dir` nicht pro Environment setzen; das funktioniert in PlatformIO nicht wie gewünscht. [web:33][web:35]

Bevorzugtes Muster:

```ini
[platformio]
src_dir = .

[env]
platform = espressif32
framework = arduino

[env:server]
board = esp32dev
build_src_filter =
    -<*>
    +<src_server/>

[env:claw1]
board = esp32dev
build_src_filter =
    -<*>
    +<src_claw1/>

[env:claw2]
board = esp32dev
build_src_filter =
    -<*>
    +<src_claw2/>
```

Mehrere Community-Beispiele nutzen genau dieses Prinzip: gemeinsamer Oberordner plus env-spezifische `build_src_filter`-Einträge für getrennte Source-Verzeichnisse. [web:25][web:26][web:28]

---

## Gemeinsamer Code
Wenn Code von mehreren Boards genutzt wird:
- gemeinsame Funktionen in `lib/` auslagern
- gemeinsame Header in `include/`
- keine Logik zwischen `src_server` und `src_claw1` duplizieren, wenn sie identisch sein kann

Board-spezifische Unterschiede über:
- klar benannte Konfigurationen
- env-spezifische `build_flags`
- getrennte Hardware-Wrapper

lösen, nicht über Copy-Paste in allen Targets. `build_flags` und `build_src_filter` sind typische Werkzeuge für Multi-Environment-Projekte. [web:29][web:35]

---

## Board-spezifischer Code
Code in `src_server/` darf Annahmen über das Server-Board treffen. Code in `src_claw1/` darf Annahmen über `claw1` treffen. Gemeinsamer Code in `lib/` darf dagegen keine festen Pins, festen Hostnamen oder eine bestimmte Rollenlogik hart codieren.

Wenn Unterschiede nötig sind:
- Konfigurationswerte klar benennen, z. B. `wifiStatusLedPin`, `clawHomeSwitchPin`, `statusTopicName`
- keine generischen Namen wie `pin1`, `pin2`, `topicA`

---

## Build und Prüfung
Vor Abschluss immer prüfen, welches Environment betroffen ist.

Typische Befehle:
- `pio run -e server`
- `pio run -e claw1`
- `pio run -e claw2`

Wenn gemeinsamer Code geändert wurde, alle betroffenen Environments bauen. PlatformIO unterstützt gezielte Builds pro Environment über `-e`, was für Multi-Board-Projekte der normale Weg ist. [web:19][web:29]

---

## Was vermieden werden soll
- Unlesbare Variablennamen.
- Copy-Paste derselben Logik in mehrere `src_*`-Ordner.
- Änderungen in falschen Target-Ordnern.
- Per-Environment-Logik über versteckte Seiteneffekte.
- Harte Annahmen im gemeinsamen Code über genau ein Board.
- Ungefragtes Umbenennen bestehender Environments oder Ordner.

---

## Bevorzugte Vorgehensweise bei Änderungen
1. Passendes Environment in `platformio.ini` identifizieren.
2. Zugehörigen `src_*`-Ordner bestimmen.
3. Prüfen, ob die Änderung nur dieses Board oder mehrere Boards betrifft.
4. Gemeinsamen Code nur dann in `lib/` auslagern, wenn er wirklich mehrfach genutzt wird.
5. Lesbare Namen für neue Variablen, Funktionen und Konstanten wählen.
6. Betroffene Environments bauen.
7. Keine nicht getesteten Hardware-Behauptungen machen.

---

## Commit-Hinweise
Commits sollten klar sagen:
- welches Board betroffen ist,
- welcher `src_*`-Ordner geändert wurde,
- ob gemeinsamer Code betroffen ist.

Beispiele:
- `fix: rename claw1 motor variables for readability`
- `feat: add status heartbeat handling for src_server`
- `refactor: move shared wifi helpers into lib`