# MQTT-Architektur Clawmachine

Stand: aus dem Code abgeleitet (`python_server/clawmachine/topics.py`, `claw_machine.py`, `moving.py`,
`src_*/main.cpp`, `lib/claw_mqtt_connection`, `lib/claw_ota`, `frontend/src/app/services/mqtt.service.ts`).

Alle Topic-Namen des Servers stehen zentral in `python_server/clawmachine/topics.py` und werden dort
aus `CLAWMACHINE_TOPIC_PREFIX` (`clawmachine`) per `device_topic(<name>, ...)` bzw. `web_interface_topic(...)`
zusammengesetzt. Das Gegenstück `parse_device_name(topic, ...)` liest den Gerätenamen wieder aus einem Topic.

## 1. Teilnehmer

| Teilnehmer | Ort | MQTT-Client-ID | Rolle |
|---|---|---|---|
| Python-Server | Server-Rechner (Docker-Container `clawmachine-server`) | `server` | Zentrale Logik: wertet Eingaben aus, steuert Motoren, führt Geräteliste |
| Motor-Controller | ESP32, `src_claw_motor_controller` | `claw_motor_controller` | Führt Motorbefehle aus (X/Y/Z, Greifer) |
| Endstop-Board | ESP32, `src_claw_server` | `claw_endstop` | Meldet Endschalter-Zustand |
| Control-Panel | ESP32, `src_claw_panel_test` | `claw_player_input` | Physische Taster des Spielers |
| Switch-2-Joy-Con | ESP32, `src_claw_switch2_scanner` (BLE) | `claw_switch2_scanner` | Joy-Con-Eingabe |
| Webinterface | Browser (Angular), per WebSocket Port 9001 | `claw_web_interface_<4-stellige-ID>` | Bedienung, Geräteliste, Anzeige |
| Emulierter ESP | `python_server/emulated_esp` | `emulated_esp` | Testgerät (Uptime, `error_log`-Dummy bzw. Steuerbefehle) |
| OTA-Skript | Entwicklungsrechner, `scripts/dev/ota_update.py` | – | Baut Firmware, publiziert Download-URL auf `<name>/ota`, wartet auf `<name>/ota/status` |
| Broker | Mosquitto, Port 1883 (MQTT) / 9001 (WebSocket) | – | Vermittler, Login Pflicht (`allow_anonymous false`) |

Alle Clients melden sich mit Benutzer/Passwort aus `config.ini` (`[mqtt]`) am Broker an.
Der Python-Server merkt sich seine Subscriptions (`MQTTClient.subscribe`) und abonniert sie nach jedem
Wiederverbinden (z. B. Broker-Neustart) automatisch neu.

## 2. Datenfluss

```
 Spieler                                                         Browser
   |                                                                |
[Control-Panel]  [Joy-Con (BLE)]                          [Webinterface]
 claw_player_input  claw_switch2_scanner                          |
   |                  |                                           |
   | player_input/panel   | player_input/joycon                     | web_interface/command
   v                  v                                           v
 +------------------------------ MQTT-Broker -----------------------------+
                          ^  |                                  ^  |
        endstop/state     |  |                web_interface/*   |  |
 [Endstop-Board] ---------+  |                 (Antworten)      |  |
                             v                                  |  |
                       [Python-Server  "server"] ---------------+  |
                             |  motor_controller/motor/command      |
                             |  motor_controller/settings           |
                             v                                      |
                     [Motor-Controller ESP]                         |
                                                                    |
 Alle Geräte: <name>/status, <name>/metadata/uptime  ---------------+--> Server + Webinterface
```

Kernprinzip: **Eingabegeräte sprechen nie direkt mit den Motoren.** Alles läuft über den Server.
Der Server ist die einzige Stelle, die auf `motor_controller/*` schreibt.
Beim Start publiziert der Server außerdem die Homing-Sequenz (`Moving.start_homing_sequence()`, aufgerufen in
`ClawMachine.__init__`), die anhand der Endstop-Meldungen die Achsen nacheinander referenziert.

Ablauf einer Spielereingabe:
1. Panel oder Joy-Con publiziert den Tastenzustand (JSON) auf `player_input/panel` bzw. `player_input/joycon`.
2. Server prüft, ob die Quelle freigegeben ist (`input_sources`), führt den Zustand mit dem letzten zusammen
   und rechnet ihn prozentual in Geschwindigkeiten um: Panel-Tasten zählen als 100 %, Joy-Con-Werte (0–100) werden direkt als Prozent genommen. Die Standard-Geschwindigkeit je Achsengruppe steht in `config.ini` (`[motor_speed]`: `xy_percent`, `z_percent`, `claw_degrees_per_second`).
3. Server publiziert pro Achse `X:<v>`, `Y:<v>`, `Z:<v>` auf `motor_controller/motor/command`.
4. Motor-Controller setzt die Achse um.

## 3. Topic-Liste

Richtung: **→ Broker** = Publisher, **← Broker** = Subscriber. `<name>` = Client-ID des Geräts.

### 3.1 Geräte-Lebenszeichen (alle ESP32, über `ClawMqttConnection`)

| Topic | Von → An | Payload | Retained | Bemerkung |
|---|---|---|---|---|
| `clawmachine/<name>/status` | ESP → Server, Webinterface | Text `online` / `offline` | ja | `online` beim Verbinden, `offline` als Last Will (QoS 1). Webinterface sendet das gleiche für sich selbst |
| `clawmachine/<name>/metadata/uptime` | ESP → Server, Webinterface | Text, Millisekunden seit Boot (z. B. `123456`) | nein | alle 5 s |
| `clawmachine/<name>/registered` | Server → Gerät | Text `ok` | nein | einmalig, wenn der Server ein Gerät zum ersten Mal sieht (automatische Registrierung) |
| `clawmachine/<name>/error_log` | ESP → Webinterface (Server wertet es nicht aus) | JSON `{"device","topic","payload","error"}` | nein | Motor-Controller meldet unverarbeitbare Befehle (ungültiges JSON, unbekannte Achse/Klauen-Aktion, keine Zahl). `payload` ist auf 100 Zeichen gekürzt. Der Emulator sendet jede Sekunde den Text `Emulated ESP is running.` |

Es gibt kein Anmelde-Topic: ein Gerät gilt als registriert, sobald es auf `status` oder `metadata/uptime` auftaucht.
Browser-Clients (`<name>` beginnt mit `claw_web_interface_`) werden bewusst nicht in die Geräteliste aufgenommen.

### 3.1.1 OTA-Update (`lib/claw_ota`, derzeit nur Motor-Controller)

| Topic | Von → An | Payload |
|---|---|---|
| `clawmachine/<name>/ota` | `ota_update.py` → Gerät | JSON `{"url":"http://<broker-host>:8000/firmware/<env>.bin"}` |
| `clawmachine/<name>/ota/status` | Gerät → `ota_update.py` | JSON `{"device","state","detail"}` mit `state` = `started`, `success`, `error`, `confirmed`, `rolled_back` |

Das Gerät lädt nur von `http://` und nur vom Broker-Host (`CLAW_MQTT_BROKER_HOST`). Die `.bin` liefert der
Python-Server unter `/firmware/` aus (Ordner `firmware_updates/`). Vor dem Flashen stoppt der Motor-Controller
alle Motoren. Die neue Firmware wird erst bestätigt (`confirmed`), wenn MQTT verbunden ist und beide Motor-Shields
am I2C-Bus antworten, sonst Rollback (`rolled_back`).

### 3.2 Eingabe → Server

| Topic | Von → An | Payload |
|---|---|---|
| `clawmachine/player_input/panel` | Panel-ESP → Server | JSON `{"up":0,"down":0,"left":0,"right":0,"front":0,"back":0,"claw":0}` (0/1), nur bei Änderung |
| `clawmachine/player_input/joycon` | Joy-Con-ESP → Server | JSON `{"up":0,"down":0,"left":0,"right":0,"front":0,"back":0}`, Werte 0–100 (Prozent: Tasten 100, Stick = Auslenkung hinter der Totzone, in 5er-Schritten), nur bei Änderung. Gleiche Feldnamen wie Panel (dort 0/1) ohne `claw` |
| `clawmachine/endstop/state` | Endstop-ESP → Server | JSON `{"x1":0,"x2":0,"y1":0,"y2":0,"z1":0}`, `1` = ausgelöst, nur bei Änderung |
| `clawmachine/web_interface/command` | Webinterface → Server | Text `<name>:<wert>`, siehe 4 |
| `clawmachine/claw` | beliebig → Server | Text `X:<v>` / `Y:<v>` / `Z:<v>` / `claw:<aktion>`, Haupt-Steuertopic (wird vom Emulator `steering.py` genutzt) |

### 3.3 Server → Motor-Controller

| Topic | Von → An | Payload |
|---|---|---|
| `clawmachine/motor_controller/motor/command` | Server → Motor-Controller | Text `X:<v>`, `Y:<v>`, `Z:<v>` (Geschwindigkeit, ganzzahlig, negativ = Gegenrichtung, `0` = Stopp) oder `claw:<aktion>` (`claw:open`, `claw:close` oder ein Winkel in Grad, z. B. `claw:120`; die Klaue begrenzt auf 90–160°). Der Motor-Controller akzeptiert zusätzlich JSON, z. B. `{"X":100,"Y":0,"Z":0,"claw":"open"}` (`"claw"` auch als Zahl, Winkel in Grad); nur enthaltene Felder werden angewendet. Der Server sendet bisher das Textformat |
| `clawmachine/motor_controller/settings` | Server → Motor-Controller | JSON mit einem oder beiden Feldern: `{"accelerationPercentPerSecond": 50.0}` (Fahrmotoren) bzw. `{"clawSpeedDegreesPerSecond": 35.0}` (Klaue, konstante Geschwindigkeit ohne Rampe, Minimum 1) |

### 3.4 Server ↔ Webinterface

| Topic | Von → An | Payload |
|---|---|---|
| `clawmachine/web_interface/devices/request` | Webinterface → Server | leer (Anfrage) |
| `clawmachine/web_interface/devices` | Server → Webinterface | JSON-Array `[{"name","isOnline","uptimeMilliseconds","addedAtUnixSeconds"}]` |
| `clawmachine/web_interface/input_sources/request` | Webinterface → Server | leer (Anfrage) |
| `clawmachine/web_interface/input_sources` | Server → Webinterface | JSON `{"panel":true,"joycon":true}`, auch ungefragt nach jeder Änderung |
| `clawmachine/web_interface/input_sources/set` | Webinterface → Server | Text `<quelle>:on` oder `<quelle>:off`, Quelle = `panel` oder `joycon` |
| `clawmachine/web_interface/speed_settings/request` | Webinterface → Server | leer (Anfrage) |
| `clawmachine/web_interface/speed_settings` | Server → Webinterface | JSON `{"xyPercent":100,"zPercent":100,"clawDegreesPerSecond":35.0}`, auch ungefragt nach jeder Änderung |
| `clawmachine/web_interface/endstop` | Server → Webinterface | durchgereichter Endstop-JSON (wie `endstop/state`) |

Das Webinterface abonniert `clawmachine/#` und füllt damit Live-Status und Geräte-Log. Die Geräteliste fragt es zusätzlich zyklisch an.

## 4. Befehle des Webinterface (`web_interface/command`)

| Payload | Wirkung des Servers |
|---|---|
| `left:<p>` / `right:<p>` | Eingabe in Prozent (-100..100, Vorzeichen = Richtung), vom Server auf die eingestellte XY-Geschwindigkeit skaliert → `motor_controller/motor/command` als `X:<v>` |
| `front:<p>` / `back:<p>` | wie oben → `Y:<v>` |
| `claw:<aktion>` | unverändert weitergereicht |
| `accel:<prozent_pro_s>` | → `motor_controller/settings` als JSON (`accelerationPercentPerSecond`) |
| `claw_speed:<grad_pro_s>` | → `motor_controller/settings` als JSON (`clawSpeedDegreesPerSecond`) |
| `xy_speed:<prozent>` / `z_speed:<prozent>` | setzt die Standard-Geschwindigkeit der Achsen am Server (Prozent der max. Drehzahl, gilt bis zum Serverneustart); antwortet auf `web_interface/speed_settings` |

## 5. Payload-Konventionen

Es gibt **keine einheitliche Struktur**, die Formate sind gewachsen. In der Praxis gelten drei Regeln:

| Art | Format | Wo |
|---|---|---|
| Zustände mit mehreren Feldern | JSON-Objekt | `player_input/*`, `endstop/state`, `input_sources`, `devices`, `motor_controller/settings` |
| Einzelne Befehle | Text `<schlüssel>:<wert>` | `motor/command`, `web_interface/command`, `input_sources/set`, `claw` |
| Einzelwerte / Flags | Reiner Text | `status` (`online`/`offline`), `metadata/uptime` (Zahl), `registered` (`ok`) |

Auffälligkeiten:
- Zwei Schreibweisen im Befehls-Bereich: `X:100` (Achsenbuchstabe groß) in `motor/command`, `left:80` (Tastenname) in `web_interface/command`. Der Server übersetzt dazwischen.
- Zwei Schreibweisen in JSON: Geräteliste in camelCase (`isOnline`), Settings ebenfalls (`accelerationPercentPerSecond`), Tastenzustände dagegen kurz und klein (`up`, `front`).
- Die Panel-Topics heißen `player_input/...`, die Client-ID des Panels aber `claw_player_input`. Topic-Segment 2 ist deshalb nicht immer die Client-ID. Gleiches gilt für `motor_controller` (Client-ID `claw_motor_controller`) und `endstop` (`claw_endstop`).

## 6. Abweichungen und tote Reste im Code

- `include/firmware_config.h`: `CLAW_DEVICE_ADDED_TOPIC "clawmachine/device/added"` ist ein Rest der alten Anmeldung und wird nicht mehr genutzt.
- `error_log` und `ota/status` werden vom Server nicht ausgewertet, nur vom Webinterface (über `clawmachine/#`) bzw. `ota_update.py` gelesen.
- Die Topic-Doku in `CLAUDE.md` nennt noch `clawmachine/joycon`, `claw/uptime_*` und ein Joy-Con-Classic-Bluetooth-Setup. Das ist überholt: der Joy-Con läuft über `player_input/joycon`, Uptime über `<name>/metadata/uptime`.
- Homing läuft nur automatisch beim Serverstart. Es gibt kein MQTT-Topic, um es zur Laufzeit neu auszulösen.
- Zwei Server mit der Client-ID `server` verdrängen sich gegenseitig vom Broker ("session taken over").

## 7. Vorschlag für eine einheitliche Struktur (noch nicht umgesetzt)

- Befehle an Geräte: `clawmachine/<name>/command`, Payload JSON `{"cmd":"move","axis":"X","speed":100}`.
- Zustände von Geräten: `clawmachine/<name>/state`, Payload JSON, retained.
- `status` und `metadata/uptime` bleiben unverändert (Last Will funktioniert nur mit einfachem Text sauber).
- Webinterface-Topics bleiben Anfrage/Antwort unter `web_interface/*`.
- Ein Feldname-Stil (camelCase) für alle JSON-Payloads.
