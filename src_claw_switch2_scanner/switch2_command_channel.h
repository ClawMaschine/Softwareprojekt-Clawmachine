#pragma once

#include <stddef.h>
#include <stdint.h>

#include "switch2_controller_types.h"
#include "switch2_session.h"

// ============================================================================
// Befehle an einen Controller schicken.
//
// Ein Befehl besteht aus drei Teilen, genau wie die Konsole ihn schickt:
//   17 Byte Praefix (Report-ID + HD-Rumble, hier Null)
//    8 Byte Befehlsheader
//    n Byte Befehlsdaten
//
// Die Characteristic kann nur WRITE_NR - eine Antwort anzufordern wuerde
// fehlschlagen. Die Bestaetigung kommt stattdessen als [CMD-RSP]-Notification.
// ============================================================================

// false = kein Befehlskanal vorhanden, Daten zu lang, oder der Write schlug
// fehl. Darf nur aus dem Arduino-Task gerufen werden, nicht aus einem
// BLE-Callback.
bool sendControllerCommand(ControllerSession *session,
                           uint8_t            commandId,
                           uint8_t            subcommandId,
                           const uint8_t     *data,
                           size_t             dataLength);

// --- Vibration --------------------------------------------------------------
//
// Vordefinierte Muster laufen als Befehl 0x0A/0x02 ueber den Befehlskanal,
// nicht ueber die Vibrations-Characteristic - die nimmt nur rohe
// HD-Rumble-Daten.
//
// Der Tastendruck wird im NimBLE-Host-Task erkannt, geschrieben wird aber im
// Arduino-Task. Ein GATT-Write aus dem Host-Task heraus kann den Stack
// blockieren, der auf genau diesen Task wartet. Deshalb nur vormerken
// (requestVibration) und im loop() ausfuehren (sendPendingVibration) -
// dasselbe Muster wie bei pendingConnectAddress und hasPendingControlState.

// Darf aus einem BLE-Callback gerufen werden: merkt nur vor.
void requestVibration(Switch2ControllerType type);

// Gehoert in den loop(). Sendet eine vorgemerkte Vibration, falls eine ansteht.
void sendPendingVibration();
