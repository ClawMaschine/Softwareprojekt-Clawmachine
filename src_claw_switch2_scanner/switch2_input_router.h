#pragma once

#include <stddef.h>
#include <stdint.h>

#include "switch2_report_tracker.h"

// ============================================================================
// Was ein eintreffender Input-Report nach sich zieht.
//
// Das ist die Antwort auf die Frage "was passiert, wenn ich eine Taste
// druecke?" - bewusst in einer eigenen, kurzen Datei. Hier und nur hier
// treffen Diagnose (switch2_report_tracker), Auswertung
// (switch2_report_parser), Vibration (switch2_command_channel) und
// MQTT-Ausgabe (joycon_control_output) zusammen. Die Module selbst kennen
// sich gegenseitig nicht.
//
// Laeuft im NimBLE-Host-Task, nicht im Arduino-Task. Deshalb wird hier nichts
// gesendet: Vibration und Steuerzustand werden nur vorgemerkt und im loop()
// abgearbeitet.
// ============================================================================

void handleIncomingReport(ReportTracker &tracker, const uint8_t *report, size_t length);
