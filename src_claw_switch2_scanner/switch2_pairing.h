#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Pairing-Handshake fuer Switch-2-Controller.
//
// Dieses Modul kennt nur das Protokoll, nicht Bluetooth. Es baut Anfragen und
// wertet Antworten aus; das Senden und Empfangen bleibt in main.cpp. Dadurch
// laesst sich der Handshake ohne Funkverkehr nachvollziehen und die
// heikle Krypto steht an einer Stelle statt verteilt im Verbindungscode.
//
// Ablauf, abgelesen aus btle_joycon2_pairing_decrypted.pcapng
// (ndeadly/switch2_controller_research), alle Befehle mit Command ID 0x15:
//
//   1. Subcommand 0x01  Host schickt seine Bluetooth-Adressen,
//                       Controller antwortet mit seiner eigenen.
//   2. Subcommand 0x04  Host schickt Public Key A1 (16 Byte),
//                       Controller antwortet mit B1.
//                       -> LTK = A1 XOR B1
//   3. Subcommand 0x02  Host schickt Challenge A2 (16 Byte),
//                       Controller antwortet mit B2. Korrekt ist B2, wenn
//                       AES-128-ECB(reverse(LTK), reverse(A2)) == B2.
//   4. Subcommand 0x03  Host bestaetigt, Controller speichert Adresse und LTK.
//
// Die Reihenfolge 0x01 -> 0x04 -> 0x02 -> 0x03 ist die der Konsole. Sie ist
// nicht aufsteigend, das ist so gemessen und kein Tippfehler.
// ============================================================================

static constexpr uint8_t COMMAND_ID_PAIRING       = 0x15;
static constexpr size_t  PAIRING_KEY_LENGTH       = 16;
static constexpr size_t  BLUETOOTH_ADDRESS_LENGTH = 6;

// Groesstes Datenfeld sind die 17 Byte von Subcommand 0x04 und 0x02.
static constexpr size_t PAIRING_MAXIMUM_REQUEST_DATA_LENGTH = 20;

// Antwortet der Controller nicht, laeuft der Handshake in eine Sackgasse.
// Zwei Sekunden sind grosszuegig: im Mitschnitt liegen zwischen Anfrage und
// Antwort nur wenige Verbindungsintervalle.
static constexpr uint32_t PAIRING_RESPONSE_TIMEOUT_MS = 2000;

enum class PairingStep : uint8_t
{
  Idle,          // nicht gestartet
  HostAddresses, // Subcommand 0x01 unterwegs
  PublicKeys,    // Subcommand 0x04 unterwegs
  Challenge,     // Subcommand 0x02 unterwegs
  Finalize,      // Subcommand 0x03 unterwegs
  Completed,
  Failed
};

struct Switch2PairingSession
{
  PairingStep step;

  uint8_t hostAddress[BLUETOOTH_ADDRESS_LENGTH];
  uint8_t controllerAddress[BLUETOOTH_ADDRESS_LENGTH];
  bool    hasControllerAddress;

  uint8_t hostPublicKey[PAIRING_KEY_LENGTH];  // A1, je Durchlauf neu gewuerfelt
  uint8_t hostChallenge[PAIRING_KEY_LENGTH];  // A2, ebenso
  uint8_t longTermKey[PAIRING_KEY_LENGTH];    // A1 XOR B1
  bool    hasLongTermKey;

  uint32_t lastRequestAtMs;
};

// Setzt die Sitzung zurueck und merkt sich die eigene Adresse. Der naechste
// Aufruf von buildPairingRequest liefert danach Subcommand 0x01.
void startPairing(Switch2PairingSession &session, const uint8_t *hostAddress);

// Baut die Anfrage fuer den aktuellen Schritt. Erzeugt dabei bei Bedarf neues
// Schluesselmaterial. false = in diesem Zustand ist nichts zu senden.
bool buildPairingRequest(Switch2PairingSession &session,
                         uint8_t              &subcommandId,
                         uint8_t              *data,
                         size_t               &dataLength);

// Verarbeitet eine Antwort des Controllers. responseData zeigt auf das
// Datenfeld NACH dem 8-Byte-Header, nicht auf den Header selbst.
// Rueckgabe true = Schritt akzeptiert, es folgt ein weiterer Aufruf von
// buildPairingRequest. false = Handshake beendet (Completed oder Failed).
bool handlePairingResponse(Switch2PairingSession &session,
                           uint8_t               subcommandId,
                           const uint8_t        *responseData,
                           size_t                length);

// Prueft auf Zeitueberschreitung. true = der Zustand hat sich zu Failed
// geaendert.
bool checkPairingTimeout(Switch2PairingSession &session, uint32_t nowMs);

const char *describePairingStep(PairingStep step);
