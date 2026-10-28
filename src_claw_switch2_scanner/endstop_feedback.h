#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Spuerbare Rueckmeldung, wenn die Maschine an einen Endschalter fahrt.
//
// Das Endstop-Board meldet bei jeder Aenderung
// {"x1":0,"x2":0,"y1":0,"y2":0,"z1":0}, 1 = ausgeloest. Beim Ausloesen eines
// Endstops (Flanke 0 -> 1) vibrieren beide Joy-Cons.
//
// Dieses Modul kennt bewusst kein MQTT: es bekommt nur den rohen Payload. Das
// Abonnement haengt in joycon_control_output, dem Modul, das die Verbindung
// besitzt.
// ============================================================================

// Welches Topic dafuer abonniert werden muss.
const char *endstopStateTopic();

// Darf nur aus dem Arduino-Task gerufen werden - requestVibration merkt
// ohnehin nur vor, gesendet wird in sendPendingVibration().
void handleEndstopStateMessage(const uint8_t *payload, size_t length);
