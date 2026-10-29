#pragma once

#include <NimBLEDevice.h>

#include <stddef.h>
#include <stdint.h>

#include "switch2_controller_types.h"
#include "switch2_pairing.h"
#include "switch2_protocol.h"
#include "switch2_report_tracker.h"

// ============================================================================
// Was diese Firmware ueber eine bestehende Controllerverbindung weiss.
//
// Die Sitzungen liegen in einem festen Feld statt auf dem Heap: auf einem
// ESP32 ist eine feste Obergrenze ehrlicher als eine Allokation, die
// irgendwann fehlschlaegt. Drei Plaetze reichen fuer zwei Joy-Cons mit Luft.
//
// Die Zeiger werden im NimBLE-Host-Task gesetzt und im Arduino-Task gelesen -
// wer hier etwas anfasst, sollte wissen, in welchem Task er laeuft.
// ============================================================================

static constexpr size_t MAXIMUM_CONTROLLER_SESSIONS = 3;

struct ControllerSession
{
  bool                  isInUse;
  NimBLEAddress         address;
  Switch2ControllerType type;
  ReportTracker         specificReportTracker;
  ReportTracker         commonReportTracker;

  // Beim Verbinden aufgeloest, nullptr wenn der Controller sie nicht anbietet.
  // Gehoeren dem NimBLEClient und werden mit ihm freigegeben - deshalb beim
  // Trennen nur die Zeiger loeschen, niemals selbst freigeben.
  NimBLERemoteCharacteristic *commandCharacteristic;
  NimBLERemoteCharacteristic *vibrationCharacteristic;

  // Befehlsantworten treffen im NimBLE-Host-Task ein und werden hier abgelegt,
  // damit Auswertung und AES im Arduino-Task laufen. Ein Slot genuegt: der
  // Controller beantwortet immer erst die laufende Anfrage.
  uint8_t       commandResponse[MAXIMUM_REPORT_LENGTH];
  size_t        commandResponseLength;
  volatile bool hasCommandResponse;

  Switch2PairingSession pairing;
};

extern ControllerSession controllerSessions[MAXIMUM_CONTROLLER_SESSIONS];

ControllerSession *findSessionByAddress(const NimBLEAddress &address);

// Zu welcher Sitzung gehoert dieser Tracker? Jede Sitzung besitzt ihre eigenen
// Tracker, die Zuordnung ist also eindeutig.
//
// Gebraucht wird sie, weil der Controllertyp als Schluessel NICHT genuegt: es
// koennen zwei LINKE Joy-Cons gleichzeitig verbunden sein. Nach Typ
// geschluesselte Zustaende ueberschreiben sich dann gegenseitig - am Geraet
// gesehen als Akkustand, der im Reporttakt zwischen zwei Werten sprang.
ControllerSession *findSessionByTracker(const ReportTracker &tracker);

// Platznummer einer Sitzung, als stabiler Schluessel je GERAET. 
// MAXIMUM_CONTROLLER_SESSIONS = kein Platz gefunden.
size_t sessionIndexOf(const ControllerSession *session);
ControllerSession *findSessionByType(Switch2ControllerType type);

// Belegt einen freien Platz und setzt ihn auf einen definierten Anfangszustand.
// nullptr = alle Plaetze besetzt.
ControllerSession *claimFreeSession(const NimBLEAddress &address);

// Gibt den Platz wieder frei. Die Characteristic-Zeiger werden nur genullt,
// nicht freigegeben: sie gehoeren dem NimBLEClient, der sich selbst loescht.
void releaseSession(ControllerSession &session);

// Setzt die Messung aller belegten Sitzungen zurueck (serielles Kommando 'r').
void resetAllSessionTrackers();
