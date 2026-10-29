#include "switch2_input_router.h"

#include <Arduino.h>
#include <stdio.h>

#include "joycon_control_output.h"
#include "joycon_power_status.h"
#include "switch2_command_channel.h"
#include "switch2_protocol.h"
#include "switch2_session.h"

// --- Kernstueck: was ein eintreffender Report nach sich zieht ---------------
//
// Diese Funktion verdrahtet nur. Sie ist bewusst die einzige Stelle, an der
// Diagnose (switch2_report_tracker), Auswertung (switch2_report_parser),
// Vibration und MQTT-Ausgabe zusammentreffen - die Module selbst kennen sich
// gegenseitig nicht.
//
// Sie laeuft im NimBLE-Host-Task, nicht im Arduino-Task. Deshalb wird hier
// nichts gesendet: Vibration und MQTT-Zustand werden nur vorgemerkt und im
// loop() abgearbeitet.

// Testvibration bei steigender Flanke der A-Taste. Bewusst VOR der Lernphase
// und nicht in der Diff-Auswertung: die laeuft erst nach der Lernphase und
// wuerde die Rueckmeldung die ersten hundert Reports lang verschlucken.
//
// Der gemeinsame Report 0x05 wird mit Typ Unknown abonniert und faellt damit
// von selbst heraus - sein Tastenlayout ist ein anderes.
static void requestVibrationOnAButtonPress(const ReportTracker &tracker,
                                           const uint8_t       *report,
                                           size_t               usableLength)
{
  if (tracker.controllerType != Switch2ControllerType::JoyConRight ||
      usableLength <= JOYCON_RIGHT_A_BUTTON_BYTE ||
      tracker.previousReportLength <= JOYCON_RIGHT_A_BUTTON_BYTE)
  {
    return;
  }

  const bool wasAButtonPressed =
      (tracker.previousReport[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;
  const bool isAButtonPressed =
      (report[JOYCON_RIGHT_A_BUTTON_BYTE] & JOYCON_RIGHT_A_BUTTON_MASK) != 0;

  if (isAButtonPressed && !wasAButtonPressed)
  {
    requestVibration(Switch2ControllerType::JoyConRight);
  }
}

// Wurde der Controller bedient - oder liegt er nur da und schickt weiter seine
// 30 Reports pro Sekunde? Daran haengt, ob eine unberuehrte Verbindung getrennt
// wird, also muss "Bedienung" eng gefasst sein:
//
//   - eine Taste hat sich geaendert (beide Tastenbytes, auch Home/C/SL/SR)
//   - oder es liegt ueberhaupt eine Richtung an (Stick gehalten zaehlt auch,
//     wenn sich der Wert nicht mehr aendert)
//
// Rauschen am Stick faellt heraus, weil es die Richtungsschwelle nicht
// erreicht und der Steuerzustand damit neutral bleibt.
static void noteControllerActivity(ReportTracker            &tracker,
                                   const uint8_t            *report,
                                   size_t                    usableLength,
                                   const JoyConControlState &controlState)
{
  bool hasActivity = !controlState.isNeutral();

  if (!hasActivity && tracker.previousReportLength > JOYCON_BUTTON_BYTE &&
      usableLength > JOYCON_BUTTON_BYTE)
  {
    for (size_t i = 0; i < JOYCON_BUTTON_BYTE_COUNT; i++)
    {
      const size_t byteIndex = JOYCON_BUTTON_BYTE + i;
      if (byteIndex >= usableLength || byteIndex >= tracker.previousReportLength)
      {
        break;
      }
      if (report[byteIndex] != tracker.previousReport[byteIndex])
      {
        hasActivity = true;
        break;
      }
    }
  }

  if (hasActivity)
  {
    tracker.lastActivityAtMs = millis();
  }
}

void handleIncomingReport(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  const size_t usableLength = length > MAXIMUM_REPORT_LENGTH ? MAXIMUM_REPORT_LENGTH : length;
  tracker.receivedReportCount++;

  printReportHexDump(tracker, report, length, usableLength);

  // Der Akkustand haengt weder an einem Vorgaengerreport noch an der
  // Lernphase - deshalb ganz vorn, noch vor jedem fruehen Ausstieg. Weiter
  // unten blieb er die ersten hundert Reports lang unsichtbar und bei einem
  // fruehen Verbindungsabriss ganz. Hinterlegt wird nur, gemeldet wird im
  // loop().
  {
    ControllerSession *session      = findSessionByTracker(tracker);
    const size_t       powerSlot    = sessionIndexOf(session);
    static char        addressText[MAXIMUM_CONTROLLER_SESSIONS][20];
    const char        *address      = "";
    if (powerSlot < MAXIMUM_CONTROLLER_SESSIONS)
    {
      // Die Adresse als Text einmal je Platz festhalten: updatePowerStatus
      // behaelt nur den Zeiger, und NimBLEAddress::toString() liefert ein
      // temporaeres Objekt.
      snprintf(addressText[powerSlot], sizeof(addressText[powerSlot]), "%s",
               session->address.toString().c_str());
      address = addressText[powerSlot];
    }
    updatePowerStatus(powerSlot, tracker.controllerName, address,
                      readPowerStateFromReport(report, usableLength));
  }

  if (captureFirstReport(tracker, report, length, usableLength))
  {
    return;  // kein Vorgaenger vorhanden, also nichts zu vergleichen
  }

  requestVibrationOnAButtonPress(tracker, report, usableLength);

  if (advanceNoiseLearning(tracker, report, usableLength))
  {
    return;  // Lernphase laeuft noch, der Report ist damit verbraucht
  }

  reportChangedBytes(tracker, report, usableLength);
  printStickDeflection(tracker, report, usableLength);

  // Beide Joy-Cons steuern die Maschine. Hier wird der Zustand lediglich
  // hinterlegt; publiziert wird im loop(), weil PubSubClient nicht threadsicher
  // ist und dieser Callback im NimBLE-Host-Task laeuft.
  // Schluessel ist das GERAET, nicht der Typ: zwei linke Joy-Cons gleichzeitig
  // sind moeglich, und nach Typ geschluesselt haben sie sich gegenseitig
  // ueberschrieben.
  const size_t sessionIndex = sessionIndexOf(findSessionByTracker(tracker));

  const JoyConControlState controlState =
      readControlStateFromReport(tracker.controllerType, report, usableLength,
                                 trackerStickCalibration(tracker),
                                 controlStateFor(sessionIndex));
  storeControlState(sessionIndex, controlState);

  noteControllerActivity(tracker, report, usableLength, controlState);

  rememberReport(tracker, report, usableLength);
}
