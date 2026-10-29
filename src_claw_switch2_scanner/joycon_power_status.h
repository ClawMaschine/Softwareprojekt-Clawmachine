#pragma once

#include <stddef.h>

#include "switch2_report_parser.h"

// ============================================================================
// Akkustand der Controller sichtbar machen.
//
// Der Wert kommt mit jedem Input-Report mit (siehe readPowerStateFromReport),
// also rund 30 mal pro Sekunde. Gemeldet wird er trotzdem nur, wenn er sich
// aendert - ein Akkustand in Dauerschleife waere weder auf der Konsole noch
// auf MQTT zu gebrauchen. Weil er sich von Natur aus selten aendert, braucht
// es dafuer keinen Timer.
//
// Geschluesselt wird je GERAET (Sitzungsplatz), nicht je Controllertyp. Zwei
// linke Joy-Cons gleichzeitig sind moeglich, und nach Typ geschluesselt haben
// sie sich gegenseitig ueberschrieben: jeder Report kippte den Wert, und
// Konsole wie MQTT liefen voll.
//
// updatePowerStatus() laeuft im NimBLE-Host-Task und merkt deshalb nur vor;
// ausgegeben und publiziert wird im loop() - dasselbe Muster wie beim
// Steuerzustand und bei der Vibration.
// ============================================================================

// Darf aus einem BLE-Callback gerufen werden: merkt nur vor. controllerName
// und address dienen nur der Ausgabe und muessen laenger leben als der Aufruf
// (Namensliteral bzw. Sitzungsfeld).
void updatePowerStatus(size_t      sessionIndex,
                       const char *controllerName,
                       const char *address,
                       const JoyConPowerState &state);

// Gehoert in den loop(). Gibt geaenderte Staende auf der Konsole aus und
// schickt sie per MQTT weiter.
void publishPendingPowerStatus();

// Serielles Kommando 'a': den bekannten Stand aller Controller ausgeben, ohne
// auf eine Aenderung zu warten.
void printPowerStatusNow();

// Beim Verbindungsverlust aufrufen. Ohne das waere der naechste Stand nach dem
// Wiederverbinden "unveraendert" und wuerde nicht gemeldet.
void clearPowerStatus(size_t sessionIndex);
