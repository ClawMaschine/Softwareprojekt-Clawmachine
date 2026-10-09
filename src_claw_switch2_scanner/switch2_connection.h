#pragma once

#include <NimBLEDevice.h>

// ============================================================================
// Controller finden, verbinden und ihre Reports abonnieren.
//
// Dieses Modul kapselt alles, was mit BLE zu tun hat:
// - Scannen und Auswerten der Nintendo-Advertisements
// - Entscheidung, ob verbunden werden darf (Pairing-Modus oder wir sind der
//   gekoppelte Host) samt Backoff gegen den Cooldown des Controllers
// - Verbindungsaufbau, Typerkennung, Abonnieren der Reports
// - Aufloesen des Befehlskanals
//
// Der Scan-Callback laeuft im NimBLE-Host-Task. Verbunden wird aber im
// Arduino-Task: der Callback merkt die Adresse nur vor, connectPendingController()
// arbeitet sie ab. Ein connect() aus dem Host-Task heraus wuerde den Stack
// blockieren, der auf genau diesen Task wartet.
// ============================================================================

// Gehoert in setup(): initialisiert NimBLE und startet den Scan.
void beginControllerConnections();

// Gehoert in den loop(): verbindet mit einem im Scan vorgemerkten Controller,
// falls einer ansteht.
void connectPendingController();
