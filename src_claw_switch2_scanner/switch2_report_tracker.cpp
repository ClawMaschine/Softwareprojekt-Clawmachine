#include "switch2_report_tracker.h"

#include <Arduino.h>
#include <string.h>

#include "switch2_hex_output.h"

ReportDiagnosticsSettings reportDiagnostics;

void resetReportTracker(ReportTracker &tracker)
{
  tracker.previousReportLength   = 0;
  tracker.hasPreviousReport      = false;
  tracker.learningSampleCount    = 0;
  tracker.isNoiseLearned         = false;
  tracker.stickCenterSumX        = 0;
  tracker.stickCenterSumY        = 0;
  tracker.stickCenterSampleCount = 0;
  tracker.hasStickCenter         = false;
  tracker.hasPrintedDeflection   = false;
  tracker.receivedReportCount    = 0;
  memset(tracker.byteChangeCount, 0, sizeof(tracker.byteChangeCount));
  memset(tracker.isNoisyByte, 0, sizeof(tracker.isNoisyByte));
}

void configureReportTracker(ReportTracker              &tracker,
                            const char                 *controllerName,
                            const char                 *reportName,
                            Switch2ControllerType       controllerType,
                            uint8_t                     firstStickOffset,
                            uint8_t                     secondStickOffset,
                            const Switch2ButtonMapping *buttonMappings,
                            size_t                      buttonMappingCount)
{
  tracker.controllerName     = controllerName;
  tracker.reportName         = reportName;
  tracker.controllerType     = controllerType;
  tracker.firstStickOffset   = firstStickOffset;
  tracker.secondStickOffset  = secondStickOffset;
  tracker.buttonMappings     = buttonMappings;
  tracker.buttonMappingCount = buttonMappingCount;
  resetReportTracker(tracker);
}

StickCalibration trackerStickCalibration(const ReportTracker &tracker)
{
  StickCalibration stick;
  stick.offset    = tracker.firstStickOffset;
  stick.center    = tracker.stickCenter;
  stick.hasCenter = tracker.hasStickCenter;
  return stick;
}

// Die drei Bytes eines Sticks gehoeren nicht in die Diff-Ausgabe: sie werden
// separat als [STICK] dekodiert. Der Rauschfilter allein reicht dafuer nicht,
// weil das dritte Byte (obere Y-Bits) im Ruhezustand stabil bleibt und sich
// erst bei Bewegung aendert - es sahe sonst wie ein Tastendruck aus.
static bool isPartOfKnownStickField(const ReportTracker &tracker, size_t byteIndex)
{
  if (tracker.firstStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.firstStickOffset &&
      byteIndex < static_cast<size_t>(tracker.firstStickOffset) + STICK_PACKED_BYTE_COUNT)
  {
    return true;
  }
  if (tracker.secondStickOffset != NO_STICK_OFFSET &&
      byteIndex >= tracker.secondStickOffset &&
      byteIndex < static_cast<size_t>(tracker.secondStickOffset) + STICK_PACKED_BYTE_COUNT)
  {
    return true;
  }
  return false;
}

static void printLearnedNoiseBytes(const ReportTracker &tracker)
{
  Serial.printf("[BASE]  %s / %s: Referenz gesetzt. Ignorierte Rausch-Bytes:",
                tracker.controllerName, tracker.reportName);

  bool hasAnyNoisyByte = false;
  for (size_t i = 0; i < tracker.previousReportLength; i++)
  {
    if (tracker.isNoisyByte[i])
    {
      Serial.printf(" 0x%02X", static_cast<unsigned>(i));
      hasAnyNoisyByte = true;
    }
  }
  if (!hasAnyNoisyByte)
  {
    Serial.print(" keine");
  }
  Serial.println();
  Serial.printf("[BASE]  %s: Stick-Mitte gemessen bei x=0x%03X y=0x%03X\n",
                tracker.controllerName, tracker.stickCenter.x, tracker.stickCenter.y);
  Serial.println("[BASE]  Bitte jetzt Tasten einzeln druecken.");
}

// --- Die Phasen eines eintreffenden Reports ---------------------------------

void printReportHexDump(const ReportTracker &tracker,
                        const uint8_t       *report,
                        size_t               length,
                        size_t               usableLength)
{
  if (!reportDiagnostics.shouldPrintFullHexDump)
  {
    return;
  }

  Serial.printf("[RAW]   %s / %s (%u Bytes): ",
                tracker.controllerName, tracker.reportName, static_cast<unsigned>(length));
  printHexBytes(report, usableLength);
  Serial.println();
}

bool captureFirstReport(ReportTracker &tracker, const uint8_t *report,
                        size_t length, size_t usableLength)
{
  if (tracker.hasPreviousReport)
  {
    return false;
  }

  rememberReport(tracker, report, usableLength);
  tracker.hasPreviousReport = true;
  Serial.printf("[BASE]  %s / %s: erster Report empfangen (%u Bytes), lerne Rauschen...\n",
                tracker.controllerName, tracker.reportName, static_cast<unsigned>(length));
  return true;
}

// Sobald genug Stichproben vorliegen: Rauschliste und Stick-Mitte festlegen.
static void finishNoiseLearning(ReportTracker &tracker)
{
  for (size_t i = 0; i < tracker.previousReportLength; i++)
  {
    const uint32_t changePercent =
        (static_cast<uint32_t>(tracker.byteChangeCount[i]) * 100) / tracker.learningSampleCount;
    tracker.isNoisyByte[i] = changePercent >= NOISE_CHANGE_PERCENT_THRESHOLD;
  }

  if (tracker.stickCenterSampleCount > 0)
  {
    tracker.stickCenter.x =
        static_cast<uint16_t>(tracker.stickCenterSumX / tracker.stickCenterSampleCount);
    tracker.stickCenter.y =
        static_cast<uint16_t>(tracker.stickCenterSumY / tracker.stickCenterSampleCount);
    tracker.hasStickCenter = true;
  }

  tracker.isNoiseLearned = true;
  printLearnedNoiseBytes(tracker);
}

bool advanceNoiseLearning(ReportTracker &tracker, const uint8_t *report, size_t usableLength)
{
  if (tracker.isNoiseLearned)
  {
    return false;
  }

  for (size_t i = 0; i < usableLength && i < tracker.previousReportLength; i++)
  {
    if (report[i] != tracker.previousReport[i])
    {
      tracker.byteChangeCount[i]++;
    }
  }

  // Dieselben Stichproben liefern die Stick-Mittellage. Der Controller liegt
  // in dieser Phase ruhig, also ist der Mittelwert genau die Ruhelage.
  if (tracker.firstStickOffset != NO_STICK_OFFSET &&
      static_cast<size_t>(tracker.firstStickOffset) + STICK_PACKED_BYTE_COUNT <= usableLength)
  {
    const StickPosition raw = unpackPackedStickValues(&report[tracker.firstStickOffset]);
    tracker.stickCenterSumX += raw.x;
    tracker.stickCenterSumY += raw.y;
    tracker.stickCenterSampleCount++;
  }

  tracker.learningSampleCount++;
  rememberReport(tracker, report, usableLength);

  if (tracker.learningSampleCount >= NOISE_LEARNING_SAMPLE_COUNT)
  {
    finishNoiseLearning(tracker);
  }
  return true;
}

void reportChangedBytes(const ReportTracker &tracker, const uint8_t *report, size_t usableLength)
{
  for (size_t i = 0; i < usableLength && i < tracker.previousReportLength; i++)
  {
    const uint8_t previousValue = tracker.previousReport[i];
    const uint8_t currentValue  = report[i];

    if (currentValue == previousValue || tracker.isNoisyByte[i] ||
        isPartOfKnownStickField(tracker, i))
    {
      continue;
    }

    uint8_t changedBits = static_cast<uint8_t>(previousValue ^ currentValue);

    // Bekannte Bits als Klartextnamen melden und aus der Restmaske entfernen.
    if (reportDiagnostics.shouldPrintButtonNames && tracker.buttonMappings != nullptr)
    {
      for (size_t m = 0; m < tracker.buttonMappingCount; m++)
      {
        const Switch2ButtonMapping &mapping = tracker.buttonMappings[m];
        if (mapping.byteOffset != i || (changedBits & mapping.bitMask) == 0)
        {
          continue;
        }

        Serial.printf("[BTN]   %s: %s %s\n",
                      tracker.controllerName,
                      mapping.buttonName,
                      (currentValue & mapping.bitMask) != 0 ? "gedrueckt" : "losgelassen");
        changedBits = static_cast<uint8_t>(changedBits & ~mapping.bitMask);
      }
    }

    if (changedBits == 0)
    {
      continue;
    }

    // Was uebrig bleibt, ist entweder nicht zugeordnet oder die Klartextausgabe
    // ist abgeschaltet. In beiden Faellen roh melden - ein unbekanntes Signal
    // darf nicht stillschweigend verschwinden.
    const uint8_t setBits     = static_cast<uint8_t>(changedBits & currentValue);
    const uint8_t clearedBits = static_cast<uint8_t>(changedBits & previousValue);

    Serial.printf("[DIFF]  %s / %s: byte 0x%02X: 0x%02X -> 0x%02X",
                  tracker.controllerName, tracker.reportName,
                  static_cast<unsigned>(i), previousValue, currentValue);
    if (setBits != 0)
    {
      Serial.printf("   bits gesetzt: 0x%02X", setBits);
    }
    if (clearedBits != 0)
    {
      Serial.printf("   bits geloescht: 0x%02X", clearedBits);
    }
    Serial.println();
  }
}

void printStickDeflection(ReportTracker &tracker, const uint8_t *report, size_t length)
{
  if (!reportDiagnostics.shouldPrintStickValues)
  {
    return;
  }

  StickDeflection deflection;
  if (!readStickDeflection(report, length, trackerStickCalibration(tracker), deflection))
  {
    return;  // kein Stick bekannt, noch nicht kalibriert, oder Report zu kurz
  }

  const bool isHorizontalIdle = isInsideDeadzone(deflection.horizontalPercent);
  const bool isVerticalIdle   = isInsideDeadzone(deflection.verticalPercent);

  if (isHorizontalIdle && isVerticalIdle)
  {
    // Mittellage: nichts ausgeben. Merker zuruecksetzen, damit die naechste
    // Auslenkung sofort wieder eine Zeile erzeugt.
    tracker.hasPrintedDeflection = false;
    return;
  }

  // Drosselung: bei ~30 Reports/s wuerde sonst jede Zitterbewegung eine Zeile
  // erzeugen. Neu ausgegeben wird erst ab einer spuerbaren Aenderung.
  if (tracker.hasPrintedDeflection)
  {
    const int16_t horizontalChange =
        abs(deflection.horizontalPercent - tracker.lastPrintedDeflection.horizontalPercent);
    const int16_t verticalChange =
        abs(deflection.verticalPercent - tracker.lastPrintedDeflection.verticalPercent);

    if (horizontalChange < STICK_PRINT_STEP_PERCENT && verticalChange < STICK_PRINT_STEP_PERCENT)
    {
      return;
    }
  }

  Serial.printf("[STICK] %s:", tracker.controllerName);
  if (!isHorizontalIdle)
  {
    Serial.printf(" %s %d%%",
                  deflection.horizontalPercent > 0 ? "rechts" : "links",
                  abs(deflection.horizontalPercent));
  }
  if (!isVerticalIdle)
  {
    Serial.printf(" %s %d%%",
                  deflection.verticalPercent > 0 ? "oben" : "unten",
                  abs(deflection.verticalPercent));
  }
  Serial.println();

  tracker.lastPrintedDeflection = deflection;
  tracker.hasPrintedDeflection  = true;
}

void rememberReport(ReportTracker &tracker, const uint8_t *report, size_t usableLength)
{
  memcpy(tracker.previousReport, report, usableLength);
  tracker.previousReportLength = usableLength;
}
