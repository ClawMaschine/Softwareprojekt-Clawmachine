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

// Gibt fuer die naechsten Sekunden JEDES Advertisement aus statt nur eines je
// Geraet und 5 Sekunden (serielles Kommando 'w'). Damit laesst sich sehen, ob
// ein Sync-Druck das Advertisement ueberhaupt veraendert - die Drosselung
// verschluckt genau diesen Moment sonst.
void startVerboseScanWindow();

// Gehoert in den loop(): trennt Controller, die eine Weile nicht bedient
// wurden. Ein daliegender Controller schickt weiter rund 30 Reports pro
// Sekunde - das kostet seinen Akku, belegt einen Sitzungsplatz und teilt sich
// die Antenne mit WiFi. Ein Tastendruck holt ihn zurueck.
void disconnectIdleControllers();

// An/aus (serielles Kommando 'i'). Beim Messen und Debuggen stoert das
// automatische Trennen, weil es im Log wie ein Abriss aussieht.
void setIdleDisconnectEnabled(bool isEnabled);
bool isIdleDisconnectEnabled();

// Versucht EINMAL, den naechsten gefundenen Switch-2-Controller zu verbinden,
// auch wenn sein Advertisement auf einen fremden Host zeigt (serielles
// Kommando 'f'). Die Annahme, dass er uns dann abweist, ist nicht mehr
// gesichert - siehe Kommentar in switch2_connection.cpp.
void forceNextConnectAttempt();

// Vergisst alle beim Scannen gemerkten Geraete samt ihrer Wartezeiten. Gehoert
// zum vollstaendigen Austragen ('x'): sonst bliebe die Wartezeit aus frueheren
// Fehlversuchen stehen, obwohl alles andere vergessen wurde.
void forgetTrackedDevices();
