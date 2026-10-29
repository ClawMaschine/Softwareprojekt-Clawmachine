#pragma once

#include <stddef.h>

#include "switch2_report_parser.h"
#include "switch2_session.h"

// ============================================================================
// Steuerbefehle an den Python-Server schicken.
//
// Dieses Modul besitzt die MQTT-Verbindung dieser Firmware. Es ist bewusst die
// einzige Stelle, die MQTT kennt - weder die Report-Auswertung noch der
// Verbindungsaufbau muessen davon wissen.
//
// Gesendet wird im Format, das der Server vom Control-Panel kennt, damit dort
// keine neue Auswertelogik noetig ist (siehe on_control_command in
// python_server/clawmachine/claw_machine.py).
//
// Der Report-Callback laeuft im NimBLE-Host-Task, maintainConnection() im
// Arduino-Task. PubSubClient ist nicht threadsicher, deshalb wird im Callback
// nur der Zustand hinterlegt (storeControlState) und im loop() publiziert
// (publishPendingControlState) - dasselbe Muster wie bei der Vibration.
//
// Weil dieses Modul die Verbindung besitzt, haengt hier auch das Abonnement
// fuer den Rueckkanal: eintreffende Nachrichten werden ungelesen an das
// zustaendige Modul weitergegeben (endstop_feedback), damit dieses kein MQTT
// kennen muss.
// ============================================================================

// Gehoeren in setup() bzw. loop().
void beginControlOutput();
void maintainControlOutput();

// --- Offline-Modus ----------------------------------------------------------
//
// Zum Testen ohne Broker (serielles Kommando 'm'). Aus heisst: WiFi wird ganz
// abgeschaltet, nichts wird gesendet, und die Steuerbefehle erscheinen als
// [OFFLINE] auf der Konsole. Die ganze Auswertung laeuft weiter - nur der
// letzte Schritt faellt weg.
//
// Das ist zugleich das Werkzeug, um einen Verbindungsabbruch von einem
// Koexistenzproblem zu unterscheiden: ohne WiFi hat BLE die Antenne fuer sich.
void setNetworkOutputEnabled(bool isEnabled);
bool isNetworkOutputEnabled();

// Nebeninformationen dieses Boards, z.B. "battery/joycon_left". Ergibt
// clawmachine/<client-id>/metadata/<metadataName> - dasselbe Schema, unter dem
// die anderen Boards ihre Uptime melden. Nur aus dem Arduino-Task rufen:
// PubSubClient ist nicht threadsicher.
void publishControllerMetadata(const char *metadataName, const char *payload);

// Darf aus einem BLE-Callback gerufen werden: merkt nur vor. Je GERAET wird
// ein eigener Zustand gehalten (Schluessel ist der Sitzungsplatz), damit alle
// zusammengefuehrt werden koennen - gleichzeitiges Druecken ergibt einen
// gemeinsamen Befehl, statt dass sich die Controller ueberschreiben.
//
// Der Controllertyp taugt dafuer NICHT als Schluessel: es koennen zwei linke
// Joy-Cons gleichzeitig verbunden sein, und die haben sich dann gegenseitig
// ausgeknockt.
void storeControlState(size_t sessionIndex, const JoyConControlState &state);

// Bei Verbindungsverlust aufrufen. Ohne das bliebe eine gehaltene Richtung
// stehen und die Maschine fuehre nach dem Abriss weiter.
void clearControlState(size_t sessionIndex);

// Der zuletzt hinterlegte Zustand dieses Geraets. Die Auswertung braucht ihn
// als Vorzustand fuer die Hysterese am Stick (siehe
// readControlStateFromReport).
JoyConControlState controlStateFor(size_t sessionIndex);

// Gehoert in den loop(). Fuehrt beide Controllerzustaende zusammen und sendet,
// falls sich etwas geaendert hat. Start und Stopp einer Richtung gehen sofort
// raus, reine Wertaenderungen innerhalb einer Bewegung nur begrenzt oft.
void publishPendingControlState();
