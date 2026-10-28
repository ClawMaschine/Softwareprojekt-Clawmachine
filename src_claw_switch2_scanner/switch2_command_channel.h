#pragma once

#include <stddef.h>
#include <stdint.h>

#include "switch2_controller_types.h"
#include "switch2_session.h"

// ============================================================================
// Befehle an einen Controller schicken.
//
// Ein Befehl besteht aus drei Teilen, genau wie die Konsole ihn schickt:
//   17 Byte Praefix (Report-ID + HD-Rumble, hier Null)
//    8 Byte Befehlsheader
//    n Byte Befehlsdaten
//
// Die Characteristic kann nur WRITE_NR - eine Antwort anzufordern wuerde
// fehlschlagen. Die Bestaetigung kommt stattdessen als [CMD-RSP]-Notification.
// ============================================================================

// false = kein Befehlskanal vorhanden, Daten zu lang, oder der Write schlug
// fehl. Darf nur aus dem Arduino-Task gerufen werden, nicht aus einem
// BLE-Callback.
bool sendControllerCommand(ControllerSession *session,
                           uint8_t            commandId,
                           uint8_t            subcommandId,
                           const uint8_t     *data,
                           size_t             dataLength);

// --- Vibration --------------------------------------------------------------
//
// Vordefinierte Muster laufen als Befehl 0x0A/0x02 ueber den Befehlskanal,
// nicht ueber die Vibrations-Characteristic - die nimmt nur rohe
// HD-Rumble-Daten.
//
// Der Tastendruck wird im NimBLE-Host-Task erkannt, geschrieben wird aber im
// Arduino-Task. Ein GATT-Write aus dem Host-Task heraus kann den Stack
// blockieren, der auf genau diesen Task wartet. Deshalb nur vormerken
// (requestVibration) und im loop() ausfuehren (sendPendingVibration) -
// dasselbe Muster wie bei pendingConnectAddress und hasPendingControlState.

// Darf aus einem BLE-Callback gerufen werden: merkt nur vor.
void requestVibration(Switch2ControllerType type);

// Gehoert in den loop(). Sendet eine vorgemerkte Vibration, falls eine ansteht.
void sendPendingVibration();

// --- Player-LEDs ------------------------------------------------------------
//
// Im Pairing-Modus laeuft das Licht ueber die vier LEDs. Ein festes Muster
// beendet das und zeigt an, dass der Controller bedient wird statt auf
// Kopplung zu warten. Wie alle Schreibbefehle nur aus dem Arduino-Task.

// Welches Muster zu welchem Controller gehoert: links die erste LED, rechts
// die zweite.
uint8_t playerLedPatternForType(Switch2ControllerType type);

// Ob beim Verbinden von selbst ein Muster gesetzt wird. **Standard aus**: der
// Befehl ist der einzige unaufgeforderte direkt nach dem Verbinden und der
// Hauptverdaechtige fuer die Abbrueche nach 2-3 s (reason=520) - der
// Projektbericht hatte die Verbindung vorher als "stabil ueber 10 s"
// abgenommen. Mit 'L' einschaltbar, falls sich der Verdacht erledigt.
void setAutomaticPlayerLedEnabled(bool isEnabled);
bool isAutomaticPlayerLedEnabled();

// Setzt NUR das Muster. Das Blinken schaltet setPlayerLedFlashing() - bewusst
// getrennt: vorher tat setPlayerLedPattern() beides, und ein abgeschaltetes
// Blinken liess sich mit keinem Kommando mehr zurueckholen.
bool setPlayerLedPattern(ControllerSession *session, uint8_t ledBitmask);

// Blinken des aktuellen Musters an/aus (Command 0x09/0x08).
bool setPlayerLedFlashing(ControllerSession *session, bool shouldFlash);

// Fuer alle verbundenen Controller, jeder mit seinem eigenen Muster
// (serielles Kommando 'l').
void setPlayerLedsForConnectedControllers();

// Alle vier LEDs an UND Blinken wieder einschalten (serielles Kommando 'o').
// Das ist der Rueckweg aus einem Controller, dem wir die Lichtrueckmeldung
// abgeschaltet haben, und zugleich der Nachweis, dass der Befehl ankommt.
// Dunkle LEDs an einem GETRENNTEN Joy-Con muessen nicht an uns liegen: ohne
// Verbindung schlaeft er ein und schaltet sie selbst ab.
void turnOnAllPlayerLeds();

// --- Kopplungsspeicher des Controllers --------------------------------------

// Laesst den Controller die gespeicherte Kopplung verwerfen - das Gegenstueck
// zum Loeschen auf unserer Seite. Dokumentiert (Command 0x03/0x08), aber im
// Mitschnitt der Konsole nicht belegt.
bool clearControllerPairingInfo(ControllerSession *session);
