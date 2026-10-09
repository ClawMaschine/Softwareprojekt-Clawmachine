#pragma once

#include <NimBLEDevice.h>

#include "switch2_pairing.h"
#include "switch2_session.h"

// ============================================================================
// Pairing am Geraet durchfuehren und dauerhaft behalten.
//
// Das Protokoll selbst steckt in switch2_pairing.* - dieses Modul taktet es
// nur: Anfrage senden, Antwort abholen, naechsten Schritt anstossen. Alles im
// Arduino-Task, weil AES und NVS im BLE-Callback nichts zu suchen haben.
//
// Dazu kommt die Persistenz: der LTK muss einen Neustart ueberleben, sonst
// waere das Pairing nach jedem Reset wertlos.
//
// Dies ist das einzige Modul, das den NimBLE-Host direkt anspricht
// (ble_gap.h, ble_store.h). Die Arduino-Huelle bietet kein Einspielen eines
// fremd ausgehandelten LTK, und Nintendo handelt den Schluessel ueber die
// eigene Befehlsschnittstelle aus statt ueber SMP.
// ============================================================================

// Beim Verbinden aufrufen. Liegt zu dieser Adresse ein gespeicherter LTK vor,
// wird er in den Schluesselspeicher des Hosts gelegt und die Verschluesselung
// angestossen. Ohne gespeicherte Kopplung passiert nichts - ein
// Verschluesselungsversuch auf gut Glueck wuerde die Trennung ausloesen.
void applyStoredPairing(NimBLEClient *client, const NimBLEAddress &address);

// Startet den Handshake fuer alle verbundenen Controller mit Befehlskanal.
// Bewusst nur auf ausdruecklichen Wunsch (serielles Kommando 'p'): ein
// Joy-Con hat genau einen Host. Nach dem Pairing verbindet er sich nicht mehr
// mit der Switch, bis er dort neu gekoppelt wird.
void startPairingForConnectedControllers();

// Gehoert in den loop(). Holt eingetroffene Befehlsantworten ab, treibt den
// Handshake weiter und prueft auf Zeitueberschreitung.
void advancePairingSessions();
