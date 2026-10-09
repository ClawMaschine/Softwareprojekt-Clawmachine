#include "switch2_input_router.h"

#include "joycon_control_output.h"
#include "switch2_command_channel.h"
#include "switch2_protocol.h"

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

void handleIncomingReport(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  const size_t usableLength = length > MAXIMUM_REPORT_LENGTH ? MAXIMUM_REPORT_LENGTH : length;
  tracker.receivedReportCount++;

  printReportHexDump(tracker, report, length, usableLength);

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
  storeControlState(tracker.controllerType,
                    readControlStateFromReport(report, usableLength,
                                               trackerStickCalibration(tracker)));

  rememberReport(tracker, report, usableLength);
}
