# Nintendo Switch 2 Joy-Con am ESP32 — Zwischenbericht

**Stand:** 11.08.2026
**Board:** ESP32 D1 Mini (`wemos_d1_mini32`, ESP32-D0WD, 4 MB Flash)
**Firmware:** `src_claw_switch2_scanner/`, Environment `claw_switch2_scanner`
**Getestete Hardware:** Joy-Con 2 Paar (L + R) einer Nintendo Switch 2

---

## Inhaltsverzeichnis

1. [Ausgangslage und Ziel](#1-ausgangslage-und-ziel)
2. [Ergebnis in einem Satz](#2-ergebnis-in-einem-satz)
3. [Warum der bisherige Projektpfad nicht funktioniert](#3-warum-der-bisherige-projektpfad-nicht-funktioniert)
4. [Technischer Lösungsweg](#4-technischer-lösungsweg)
5. [Neue Erkenntnisse aus eigener Messung](#5-neue-erkenntnisse-aus-eigener-messung)
6. [Gemessene Rohdaten](#6-gemessene-rohdaten)
7. [Gefundene und behobene Fehler](#7-gefundene-und-behobene-fehler)
8. [Stand der Verifikation](#8-stand-der-verifikation)
9. [Offene Punkte](#9-offene-punkte)
10. [Quellen](#10-quellen)

---

## 1. Ausgangslage und Ziel

Die Clawmachine soll mit einem Nintendo-Switch-2-Joy-Con gesteuert werden. Der im Projekt
vorhandene Ansatz zielte auf Switch-**1**-Controller über Bluetooth Classic mit der Bibliothek
Bluepad32. Dieser Code liegt in `src_claw_player_input/` vollständig auskommentiert und ist
nicht baubar — Bluepad32 ist in keinem `lib_deps` eingetragen und das erwartete
`local_components/`-Verzeichnis existiert nicht.

Ziel dieses Arbeitsschritts war bewusst eng gefasst: **rohe Eingabesignale beider Joy-Con 2
sichtbar machen**, ohne WiFi, ohne MQTT und ohne vorab festgelegte Tastenbelegung. Die
Zuordnung „Bit → physische Taste" sollte durch Drücken und Ablesen entstehen, nicht durch
Übernahme aus fremder Dokumentation.

---

## 2. Ergebnis in einem Satz

**Es funktioniert.** Beide Joy-Con 2 verbinden sich gleichzeitig mit einem einzelnen
ESP32 D1 Mini, werden korrekt als links bzw. rechts erkannt und liefern Tastendrücke und
Stickpositionen in Echtzeit auf die serielle Konsole.

Der Ressourcenverbrauch ist unkritisch: **RAM 11,1 %** (36.292 von 327.680 Bytes),
**Flash 47,7 %** (624.890 von 1.310.720 Bytes). Es bleibt reichlich Platz für WiFi, MQTT und
die eigentliche Spiellogik.

---

## 3. Warum der bisherige Projektpfad nicht funktioniert

Nintendo hat bei der Switch 2 die Controller-Anbindung komplett neu und proprietär gebaut.
Vier Punkte machen den Switch-1-Ansatz unbrauchbar:

**Bluetooth Classic entfällt.** Switch-1-Joy-Cons sprechen BR/EDR-HID. Switch-2-Controller
sprechen ausschließlich Bluetooth Low Energy. Bluepad32 unterstützt laut eigener Dokumentation
nur BR/EDR-Nintendo-Geräte — Switch-2-Controller sind dort nicht gelistet und werden es
absehbar auch nicht, weil das Protokoll grundverschieden ist.

**Kein HID over GATT.** Statt des Standardprofils HOGP implementieren die Controller eine
eigene, HID-ähnliche Schnittstelle über proprietäre GATT-Attribute. Kein generischer
HID-Treiber kann damit umgehen.

**Kein SMP-Pairing — und Pairing-Versuche sind aktiv schädlich.** Nintendo tauscht
Schlüsselmaterial über die eigene Befehlsschnittstelle aus (Pseudo-Out-of-Band-Verfahren,
`LTK = A1 XOR B1`, Bestätigung per AES-128-ECB). Versucht ein Host das übliche
SMP-Pairing, trennt der Controller die Verbindung sofort. Viele Bluetooth-Stacks tun das
standardmäßig — das ist die häufigste Fehlerquelle.

**Pairing ist für unseren Zweck aber gar nicht nötig.** Die Dokumentation hält fest, dass
Pairing nur erforderlich ist, damit der Controller sich später selbstständig wieder mit der
Konsole verbindet oder sie aufweckt. Zum reinen Mitlesen von Eingaben genügt eine offene,
unverschlüsselte BLE-Verbindung. **Das haben wir praktisch bestätigt.**

---

## 4. Technischer Lösungsweg

### Verbindungsablauf

1. BLE-Scan mit aktivem Scanning
2. Filter auf Manufacturer-Specific-Data mit Nintendo-Kennung `0x0553`
3. Prüfung von Vendor ID (`0x057E`) und Product ID (Switch 2 ab `0x2060`)
4. Verbinden **ohne** Bonding, MITM oder Secure Connections
5. MTU-Aushandlung erzwingen
6. Vollständigen GATT-Baum auslesen und protokollieren
7. Controllertyp über die Characteristic-UUID auf Handle `0x000E` bestimmen
8. Auf die typspezifische Characteristic subscriben
9. Scan fortsetzen, um den zweiten Joy-Con zu finden

### Typerkennung

Nintendo stellt kein Typfeld bereit. Der Typ ergibt sich daraus, **welche Characteristic auf
Handle `0x000E` existiert** — die UUID ist pro Controllertyp eindeutig:

| UUID auf `0x000E` | Controllertyp | Report |
| --- | --- | --- |
| `cc1bbbb5-7354-4d32-a716-a81cb241a32a` | Joy-Con 2 (L) | 0x07 |
| `d5a9e01e-2ffc-4cca-b20c-8b67142bf442` | Joy-Con 2 (R) | 0x08 |
| `7492866c-ec3e-4619-8258-32755ffcc0f8` | Pro Controller 2 | 0x09 |
| `8261cba1-9435-420c-84d6-f0c75a2c8e4d` | NSO GameCube | 0x0A |

Diese Erkennung wurde **dreifach unabhängig bestätigt**: die UUIDs auf den Handles `0x000E`
(Input), `0x0012` (Vibration) und `0x001E` (Command Response) gehören bei beiden getesteten
Geräten jeweils alle drei konsistent zum selben Controllertyp.

### Rohsignal-Auswertung

Die Firmware trifft keine Annahme über Tastenbelegungen. Pro abonnierter Characteristic wird
der letzte Report aufgehoben und mit dem nächsten verglichen:

1. **Erster Report** → Referenz
2. **Lernphase über 100 Reports** → gezählt, welche Bytes sich bei ruhendem Controller ändern.
   Bytes mit einer Änderungsrate ≥ 25 % gelten als Rauschen und werden danach ausgeblendet.
   Die Liste wird protokolliert, damit nichts unsichtbar verschwindet.
3. **Auswertephase** → jedes geänderte Nicht-Rausch-Byte erzeugt eine Zeile mit Offset,
   altem und neuem Wert sowie den per XOR ermittelten gesetzten bzw. gelöschten Bits.

Zusätzlich werden die bekannten Stick-Bytes separat als 12-Bit-Werte dekodiert und aus der
Diff-Ausgabe ausgeschlossen.

Zur Laufzeit über die serielle Schnittstelle umschaltbar, ohne neu zu flashen:
`d` Vollhexdump, `r` Referenz zurücksetzen, `s` Stick-Ausgabe, `h` Hilfe.

### Werkzeugkette

| Komponente | Version |
| --- | --- |
| PlatformIO Core | 6.1.19 |
| Platform | pioarduino espressif32 54.03.21 |
| Arduino-Core | 3.2.1 |
| ESP-IDF | 5.4 |
| BLE-Stack | NimBLE-Arduino 2.5.1 |

NimBLE statt Bluedroid, weil es sich als `lib_deps` eintragen lässt, rund 100 KB weniger RAM
braucht und von sich aus kein SMP-Pairing initiiert.

---

## 5. Neue Erkenntnisse aus eigener Messung

Diese Punkte stehen in **keiner** der ausgewerteten Quellen und sind Ergebnis der eigenen
Messung.

### 5.1 Joy-Con 2 brauchen keine Init-Befehlssequenz

Das war die einzige Frage, die die Recherche nicht klären konnte. Das Linux-Projekt
`trevlars/switch2-controllers-linux` vermerkt ausdrücklich *„Joy-Con 2 remains untested"*, und
der einzige belegte ESP32-Erfolg betraf den Pro Controller 2.

**Gemessen: reines Subscribe genügt.** Nach dem Schreiben des CCC-Descriptors senden beide
Joy-Con 2 sofort Input-Reports. Die als Fallback vorgesehene Befehlssequenz
(`Command 0x0C / Subcommand 0x06 – Configure Features`) wird nicht benötigt.

### 5.2 Product IDs der Joy-Con 2

Die Dokumentation sagt lediglich *„Switch 2 product IDs begin from `0x2060`"* und schlüsselt
die einzelnen Geräte nicht auf. Gemessen:

| Product ID | Controllertyp | Bluetooth-Adresse des Testgeräts |
| --- | --- | --- |
| `0x2066` | Joy-Con 2 **(R)** | `b8:68:70:8a:19:94` |
| `0x2067` | Joy-Con 2 **(L)** | `b8:68:70:89:8c:87` |

Damit lässt sich der Typ bereits **vor** dem Verbindungsaufbau aus dem Advertisement ableiten.
Die UUID-basierte Erkennung nach dem Verbinden bleibt trotzdem die verlässlichere Methode und
sollte als maßgeblich behandelt werden — die Product IDs sind an zwei Geräten gemessen, nicht
an einer Serie.

### 5.3 Motion-Daten sind standardmäßig abgeschaltet

Die Reports sind 63 Bytes lang. Als veränderlich erkannt wurden im Ruhezustand nur die Bytes
`0x00` (Zähler), `0x05` und `0x06` (Stick). Die Bytes `0x10`–`0x38`, in denen laut
Dokumentation die Motion-Daten liegen, blieben konstant.

Das bedeutet: **Beschleunigungs- und Gyroskopdaten werden erst gesendet, wenn Feature-Bit 2
aktiv gesetzt wird.** Für die Clawmachine ist das günstig — die Diff-Ausgabe bleibt von sich
aus sauber, und es wird keine Bandbreite verschwendet.

### 5.4 Der Pairing-Modus ist zwingend erforderlich

Ein bereits an eine Konsole gekoppelter Joy-Con sendet ein *Reconnection-Advertisement* und
**lehnt fremde Verbindungen ab**. Der Fehler lautet `reason=574`, das ist in NimBLE-Kodierung
der HCI-Fehler `0x3E` (*Connection Failed to be Established*).

Erkennbar ist der Zustand direkt im Advertisement an den Bytes `0xC`–`0x11` der
Manufacturer-Daten:

```
Pairing-Modus (Sync-Taste gedrückt):
53050100 037E0566 20000100 00000000 00000F00 ...
                            └── Host-Adresse: alles 0x00 ──┘

Bereits gekoppelt:
53050100 037E0567 20000100 FDFE32F7 44400F00 ...
                            └─ Host-Adresse belegt ─┘
```

Das bestätigt das dokumentierte Feldlayout empirisch. **Praktische Konsequenz:** vor jedem
Verbindungsaufbau muss die Sync-Taste gedrückt gehalten werden.

### 5.5 Die Dokumentation ist exemplarunabhängig zuverlässig

Ein Praxisbericht hatte gewarnt, dass Characteristic-UUIDs zwischen einzelnen Geräten
abweichen können. **Für unsere beiden Joy-Con 2 trifft das nicht zu** — der vollständige
GATT-Baum stimmt in jeder UUID und jedem Handle exakt mit der Dokumentation überein.

Bestätigt wurde dabei auch der dokumentierte Sonderfall, dass der `Generic Attribute`-Service
(`0x1801`) keine Kindattribute besitzt (Handle-Bereich `0x0030`–`0x0030`). NimBLE kommt damit
problemlos zurecht — anders als bei manchen anderen Stacks.

---

## 6. Gemessene Rohdaten

### 6.1 GATT-Baum eines Joy-Con 2

Identisch bei beiden Geräten bis auf die typspezifischen UUIDs (hier: linker Joy-Con):

| Service | Handles |
| --- | --- |
| `00c5af5d-1964-4e30-8f51-1956f96bd280` | `0x0001`–`0x0007` |
| `ab7de9be-89fe-49ad-828f-118f09df7fd0` | `0x0008`–`0x002A` |
| `0x1800` (Generic Access) | `0x002B`–`0x002F` |
| `0x1801` (Generic Attribute, leer) | `0x0030`–`0x0030` |

Characteristics im Hauptservice `ab7de9be-…-fd0`:

| Handle | UUID | Eigenschaften | Bedeutung |
| --- | --- | --- | --- |
| `0x000A` | `ab7de9be-…-fd2` | READ, NOTIFY | Input Report 0x05 (alle Typen) |
| `0x000E` | `cc1bbbb5-…-1a32a` | READ, NOTIFY | Input Report 0x07 (nur L) |
| `0x0012` | `289326cb-…-18241` | WRITE_NR | Vibration (nur L) |
| `0x0014` | `649d4ac9-…-f005` | WRITE_NR | Befehle (alle Typen) |
| `0x0016` | `ce49a830-…-dbea` | WRITE_NR | Vibration + Befehl (nur L) |
| `0x0018` | `4147423d-…-9f8d` | WRITE_NR | Firmware-Update |
| `0x001A` | `c765a961-…-836a` | NOTIFY | Befehlsantwort |
| `0x001E` | `63a3810f-…-b996` | NOTIFY | Erweiterte Befehlsantwort (nur L) |
| `0x0022` | `d3bd69d2-…-2a80` | NOTIFY | unbekannt |
| `0x0026` | `ab7de9be-…-fde` | READ, NOTIFY | unbekannt |
| `0x002A` | `ab7de9be-…-fdf` | WRITE_NR | unbekannt |

Beim rechten Joy-Con stehen an den Stellen `0x000E`, `0x0012`, `0x0016` und `0x001E` die
Rechts-Varianten `d5a9e01e-…`, `fa19b0fb-…`, `65a724b3-…` und `640ca58e-…`.

### 6.2 Verbindungsparameter

| Größe | Wert |
| --- | --- |
| Ausgehandelte MTU | 247 Bytes |
| Reportlänge | 63 Bytes |
| Verbindungsintervall | 15–30 ms (eingestellt) |
| Gleichzeitige Verbindungen | 2 (getestet), 3 möglich |

Anmerkung: die Switch-Konsole nutzt laut Dokumentation ein Intervall von 5 ms. Das liegt
unterhalb des in der Bluetooth-Spezifikation vorgeschriebenen Minimums von 7,5 ms und ist mit
dem ESP32 nicht erreichbar. Für einen Greifautomaten ist das ohne Belang.

### 6.3 Beobachtete Button-Bits

Alle bisher erfassten Tastendrücke lagen auf **Byte `0x02`** des Reports:

| Maske | Anzahl Betätigungen |
| --- | --- |
| `0x01` | 4 |
| `0x02` | 3 |
| `0x04` | 7 |
| `0x08` | 5 |
| `0x10` | 3 |
| `0x20` | 1 |

Alle sechs liegen an Positionen, an denen die Dokumentation Tasten erwartet. **Welche Maske zu
welcher physischen Taste gehört, ist noch nicht belegt** — dazu fehlt die Aufzeichnung, welche
Taste jeweils gedrückt wurde (siehe [Offene Punkte](#9-offene-punkte)).

### 6.4 Stick-Wertebereiche

Sticks sind als zwei 12-Bit-Werte in drei Bytes gepackt, ab Offset `0x05`:

```
x = b[0] | ((b[1] & 0x0F) << 8)
y = (b[1] >> 4) | (b[2] << 4)
```

| Gerät | Achse | Minimum | Maximum | Ruhelage |
| --- | --- | --- | --- | --- |
| Joy-Con 2 (R) | X | `0x385` | `0xCE4` | — |
| Joy-Con 2 (R) | Y | `0x2E8` | `0xC70` | — |
| Joy-Con 2 (L) | X | — | — | `0x7B8` |
| Joy-Con 2 (L) | Y | — | — | `0x8CA` |

Die Werte sind **deutlich unsymmetrisch** um die dokumentierte theoretische Mitte `0x800`. Die
Dokumentation bezeichnet die Werte ausdrücklich als *uncalibrated*. Für die Clawmachine muss
die Ruhelage daher pro Gerät gemessen und eine Deadzone daraus abgeleitet werden — sie darf
nicht angenommen werden.

---

## 7. Gefundene und behobene Fehler

Alle drei traten erst beim Test mit echter Hardware zutage.

**Stick-Byte erschien als Tastendruck.** Byte `0x07` ist das dritte Stick-Byte und enthält die
oberen Y-Bits. Im Ruhezustand ist es stabil und wurde deshalb vom Rauschfilter nicht erfasst —
bei Stickbewegung tauchte es dann als vermeintlicher Tastendruck in der Diff-Ausgabe auf.
Behoben durch expliziten Ausschluss der bekannten Stick-Bytefelder.

**Verbindungsschleife ohne Backoff.** Nach einem Fehlschlag wurde sofort neu gescannt und
verbunden — fünf Versuche in acht Sekunden, dazu eine Protokollzeile pro Advertisement. Das
ließ den UART-Puffer überlaufen (ineinander verschachtelte Zeilen) und provoziert genau den
dokumentierten Cooldown, der den Controller minutenlang blockiert. Behoben durch gestaffelten
Backoff von 5 s bis 30 s und Drosselung der Scan-Protokollierung auf eine Zeile je Gerät und
5 Sekunden.

**Zugriff auf freigegebenen Speicher.** Die Einstellung `setSelfDelete(true, true)` veranlasst
NimBLE, das Client-Objekt bei einem Verbindungsfehler selbst zu löschen. Die anschließende
Abfrage `client->getLastError()` griff damit auf bereits freigegebenen Speicher zu. Behoben
durch `setSelfDelete(true, false)` mit manuellem `deleteClient()`.

---

## 8. Stand der Verifikation

| # | Kriterium | Status |
| --- | --- | --- |
| 1 | Nintendo-Advertisement mit Product ID ≥ `0x2060` erkannt | erfüllt |
| 2 | Verbindung steht, MTU > 23, stabil über 10 s | erfüllt (MTU 247) |
| 3 | GATT-Dump zeigt Hauptservice und Typ-UUID | erfüllt |
| 4 | Erkannter Typ stimmt mit physischem Joy-Con überein | erfüllt |
| 5 | Tastendruck erzeugt Diff-Zeile | erfüllt |
| 6 | Jede Taste liefert eindeutige, reproduzierbare Maske | **offen** |
| 7 | Stickwerte plausibel | erfüllt |
| 8 | Beide Joy-Cons gleichzeitig verbunden | erfüllt |
| 9 | Mapping-Dokument vollständig | **offen** |

Sieben von neun Kriterien sind erfüllt. Die beiden offenen Punkte hängen am selben fehlenden
Baustein.

---

## 9. Offene Punkte

**Zuordnung Bit → physische Taste.** Aus dem Protokoll ist ersichtlich, dass sechs Bits leben,
aber nicht, welche Taste jeweils gedrückt wurde. `0x04` lässt sich ohne diese Information nicht
von „Y" unterscheiden. Vorgeschlagene Lösung: ein geführter Mapping-Modus in der Firmware, der
der Reihe nach zum Drücken einzelner Tasten auffordert, auf genau einen Diff wartet und am Ende
die fertige Tabelle ausgibt.

**Noch nicht beobachtete Bits.** Auf Byte `0x02` fehlen `0x40` und `0x80`, auf Byte `0x03`
wurde bisher überhaupt kein Bit gesehen. Dort erwartet die Dokumentation SL, SR, C und Home.

**Linker Joy-Con.** Verbindung, Typerkennung und Stick sind bestätigt, es wurde aber noch keine
Taste gedrückt.

**Stick-Kalibrierung.** Ruhelage und Anschläge müssen pro Gerät gemessen werden.

**WiFi/BLE-Koexistenz.** Bisher ungetestet. Der ESP32 teilt sich eine Antenne zwischen WiFi
und Bluetooth. Beim Zusammenführen mit dem MQTT-Teil in `claw_player_input` ist das der
wahrscheinlichste Problempunkt. Rückfallebene: Verbindungsintervall entspannen oder BLE und
MQTT auf zwei ESP32 aufteilen.

**Reconnect nach Neustart.** Ohne Pairing muss nach jedem Neustart die Sync-Taste gedrückt
werden. Für einen dauerhaft laufenden Greifautomaten ist das unpraktisch. Der dokumentierte
Pairing-Handshake (`LTK = A1 XOR B1`, AES-128-ECB-Bestätigung) wäre umsetzbar, war für diesen
Arbeitsschritt aber nicht nötig.

---

## 10. Quellen

### Primärquelle

**[ndeadly/switch2_controller_research](https://github.com/ndeadly/switch2_controller_research)**
— die maßgebliche Reverse-Engineering-Dokumentation. Alle UUIDs, Handles und Byte-Layouts
stammen daraus.

| Datei | Verwendung |
| --- | --- |
| [`bluetooth_interface.md`](https://github.com/ndeadly/switch2_controller_research/blob/master/bluetooth_interface.md) | Advertisement-Format, GATT-Attributtabelle, Pairing-Verfahren, Protokolleinschränkungen |
| [`hid_reports.md`](https://github.com/ndeadly/switch2_controller_research/blob/master/hid_reports.md) | Report-Zuordnung, Layout von Report 0x05 / 0x07 / 0x08, Button-Bitfelder |
| [`commands.md`](https://github.com/ndeadly/switch2_controller_research/blob/master/commands.md) | Befehlsheader-Format, Fallback-Befehle |

Ergänzend das Referenz-Python-Skript desselben Autors:
[gist 7d27aa63e2f653a902a2474dbcbc08b3](https://gist.github.com/ndeadly/7d27aa63e2f653a902a2474dbcbc08b3)

### Praxisreferenz

**[Qiita — „M5Stack(ESP32)にSwitch2のプロコン2を無線接続してみた" von Tsukusim](https://qiita.com/Tsukusim/items/5a88b76a2e8e0e69e412)**
— funktionierende NimBLE-Implementierung auf einem ESP32, die belegt, dass ein normaler ESP32
ohne PSRAM ausreicht.

### Quervergleich

| Projekt | Beitrag |
| --- | --- |
| [trevlars/switch2-controllers-linux](https://github.com/trevlars/switch2-controllers-linux) | Vermerkt „Joy-Con 2 remains untested" — die Lücke, die dieser Bericht schließt |
| [TommyWabg/Switch2Connect](https://github.com/TommyWabg/Switch2Connect) | Dokumentiert den Verbindungs-Cooldown bei zu häufigen Versuchen |

### Bibliotheken

| Ressource | Verwendung |
| --- | --- |
| [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.5.1 | BLE-Stack |
| [NimBLE 1.x→2.x Migrationsleitfaden](https://github.com/h2zero/NimBLE-Arduino/blob/master/docs/1.x_to2.x_migration_guide.md) | API-Änderungen; ältere Tutorials im Netz nutzen noch die 1.x-API |
| [Bluepad32 — Supported gamepads](https://bluepad32.readthedocs.io/en/latest/supported_gamepads/) | Negativbeleg: keine Switch-2-Unterstützung |
