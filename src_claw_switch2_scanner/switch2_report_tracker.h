#pragma once

#include <stddef.h>
#include <stdint.h>

#include "switch2_controller_types.h"
#include "switch2_protocol.h"
#include "switch2_report_parser.h"

// ============================================================================
// Report-Verfolgung: das Diagnosewerkzeug dieser Firmware.
//
// Pro abonnierter Characteristic wird der letzte Report aufgehoben und mit dem
// naechsten verglichen. Bytes, die sich im Ruhezustand staendig aendern
// (Zaehler, Motion-Daten, Stick-Jitter), werden in einer Lernphase automatisch
// als Rauschen markiert und danach aus der Diff-Ausgabe ausgeblendet - sonst
// waere die Konsole unbrauchbar. Der Vollhexdump zeigt trotzdem alles.
//
// Dieses Modul beobachtet und gibt aus. Es loest KEINE Aktionen aus: es kennt
// weder MQTT noch Vibration noch Bluetooth. Die Reihenfolge der Phasen legt
// der Aufrufer fest (siehe handleIncomingReport), damit genau an einer Stelle
// steht, was ein eintreffender Report nach sich zieht.
// ============================================================================

static constexpr uint16_t NOISE_LEARNING_SAMPLE_COUNT    = 100;
static constexpr uint8_t  NOISE_CHANGE_PERCENT_THRESHOLD = 25;

// Drosselung der [STICK]-Ausgabe: erst ab dieser Aenderung in Prozent wird
// eine neue Zeile erzeugt.
static constexpr int8_t STICK_PRINT_STEP_PERCENT = 5;

// Laufzeitschalter der Diagnoseausgabe. Ueber die serielle Konsole
// umschaltbar, damit zum Nachmessen kein Neuflashen noetig ist.
struct ReportDiagnosticsSettings
{
  bool shouldPrintFullHexDump = false;
  bool shouldPrintStickValues = true;
  // true = Klartextnamen, false = rohe Bytes und Bitmasken (zum Nachmessen)
  bool shouldPrintButtonNames = true;
};

extern ReportDiagnosticsSettings reportDiagnostics;

struct ReportTracker
{
  // --- Beim Abonnieren einmal gesetzt, danach unveraendert ---
  const char           *controllerName;
  const char           *reportName;
  Switch2ControllerType controllerType;
  uint8_t               firstStickOffset;
  uint8_t               secondStickOffset;

  const Switch2ButtonMapping *buttonMappings;
  size_t                      buttonMappingCount;

  // --- Vergleichsbasis fuer den naechsten Report ---
  uint8_t previousReport[MAXIMUM_REPORT_LENGTH];
  size_t  previousReportLength;
  bool    hasPreviousReport;

  // --- Lernphase: welche Bytes rauschen? ---
  uint16_t learningSampleCount;
  uint16_t byteChangeCount[MAXIMUM_REPORT_LENGTH];
  bool     isNoisyByte[MAXIMUM_REPORT_LENGTH];
  bool     isNoiseLearned;

  // Mittellage wird waehrend derselben Lernphase mitgemittelt, in der auch das
  // Rauschen bestimmt wird. Die Ruhelage liegt geraeteabhaengig deutlich neben
  // den theoretischen 0x800 (gemessen: 0x860 / 0x788) und darf deshalb nicht
  // angenommen werden.
  uint32_t      stickCenterSumX;
  uint32_t      stickCenterSumY;
  uint16_t      stickCenterSampleCount;
  StickPosition stickCenter;
  bool          hasStickCenter;

  // --- Drosselung der Stick-Ausgabe ---
  StickDeflection lastPrintedDeflection;
  bool            hasPrintedDeflection;

  uint32_t receivedReportCount;

  // Wann wurde dieser Controller zuletzt wirklich bedient - Taste oder Stick,
  // nicht blosses Eintreffen von Reports (die kommen 30 mal pro Sekunde, auch
  // wenn er nur daliegt). Daran haengt das Trennen unberuehrter Verbindungen.
  uint32_t lastActivityAtMs;
};

// Setzt die Messung zurueck. Die beim Abonnieren gesetzten Felder
// (controllerName, Offsets, Tastenbelegung) bleiben bewusst erhalten - nach
// einem 'r' auf der Konsole soll derselbe Controller neu vermessen werden,
// nicht ein anderer.
void resetReportTracker(ReportTracker &tracker);

// Uebernimmt Name, Typ, Stick-Offsets und Tastenbelegung aus der
// Controllerbeschreibung und setzt die Messung zurueck.
void configureReportTracker(ReportTracker                   &tracker,
                            const char                      *controllerName,
                            const char                      *reportName,
                            Switch2ControllerType            controllerType,
                            uint8_t                          firstStickOffset,
                            uint8_t                          secondStickOffset,
                            const Switch2ButtonMapping      *buttonMappings,
                            size_t                           buttonMappingCount);

// Der Parser soll den Tracker nicht kennen muessen. Deshalb wird die
// Kalibrierung des ersten Sticks hier zusammengestellt und als Wert
// uebergeben - die Abhaengigkeit laeuft damit nur nach unten.
StickCalibration trackerStickCalibration(const ReportTracker &tracker);

// --- Die Phasen eines eintreffenden Reports ---------------------------------
//
// Der Aufrufer ruft sie in dieser Reihenfolge auf. Die beiden Funktionen mit
// bool-Rueckgabe melden "Report ist verbraucht, hoer hier auf".

// Vollhexdump, falls eingeschaltet. length ist die echte Laenge des Reports,
// usableLength die auf MAXIMUM_REPORT_LENGTH begrenzte.
void printReportHexDump(const ReportTracker &tracker,
                        const uint8_t       *report,
                        size_t               length,
                        size_t               usableLength);

// Allererster Report nach dem Abonnieren: es gibt noch nichts zu vergleichen,
// er wird nur zur Vergleichsbasis. true = genau das war der Fall.
bool captureFirstReport(ReportTracker &tracker, const uint8_t *report,
                        size_t length, size_t usableLength);

// Lernphase: zaehlen, welche Bytes sich im Ruhezustand staendig aendern, und
// dabei die Stick-Mittellage mitteln. true = es wird noch gelernt.
bool advanceNoiseLearning(ReportTracker &tracker, const uint8_t *report, size_t usableLength);

// Auswertephase: geaenderte Bytes melden - bekannte Bits als Tastennamen, der
// Rest roh. Ein unbekanntes Signal darf nicht stillschweigend verschwinden.
void reportChangedBytes(const ReportTracker &tracker, const uint8_t *report, size_t usableLength);

// Stick als Richtung und prozentuale Auslenkung ausgeben. In der Mittellage
// wird bewusst gar nichts ausgegeben.
void printStickDeflection(ReportTracker &tracker, const uint8_t *report, size_t length);

// Report als Vergleichsbasis fuer den naechsten merken.
void rememberReport(ReportTracker &tracker, const uint8_t *report, size_t usableLength);
