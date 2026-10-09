#pragma once

#include "switch2_controller_types.h"
#include "switch2_report_parser.h"

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
// ============================================================================

// Gehoeren in setup() bzw. loop().
void beginControlOutput();
void maintainControlOutput();

// Darf aus einem BLE-Callback gerufen werden: merkt nur vor. Je Controller
// wird ein eigener Zustand gehalten, damit beide zusammengefuehrt werden
// koennen - gleichzeitiges Druecken links und rechts ergibt einen gemeinsamen
// Befehl, statt dass sich beide gegenseitig ueberschreiben.
void storeControlState(Switch2ControllerType type, const JoyConControlState &state);

// Bei Verbindungsverlust aufrufen. Ohne das bliebe eine gehaltene Richtung
// stehen und die Maschine fuehre nach dem Abriss weiter.
void clearControlState(Switch2ControllerType type);

// Gehoert in den loop(). Fuehrt beide Controllerzustaende zusammen und sendet,
// falls sich etwas geaendert hat.
void publishPendingControlState();
