#include "switch2_pairing_manager.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

// Fuer die gespeicherte Kopplung wird der NimBLE-Host direkt angesprochen:
// die Arduino-Huelle bietet kein Einspielen eines fremd ausgehandelten LTK.
#include "nimble/nimble/host/include/host/ble_gap.h"
#include "nimble/nimble/host/include/host/ble_store.h"

#include "switch2_command_channel.h"
#include "switch2_protocol.h"

// --- Gespeicherte Kopplungen ------------------------------------------------
//
// Der LTK muss einen Neustart ueberleben, sonst waere das Pairing nach jedem
// Reset wertlos. Abgelegt wird er unter der Adresse des Controllers, damit
// beide Joy-Cons nebeneinander passen.

static Preferences pairingStorage;

static constexpr const char *PAIRING_STORAGE_NAMESPACE = "switch2";

// NVS-Schluessel duerfen hoechstens 15 Zeichen lang sein. Die Adresse als
// Hexkette ohne Trenner braucht genau 12.
static void buildStorageKey(const NimBLEAddress &address, char *keyBuffer, size_t keyBufferLength)
{
  const uint8_t *addressBytes = address.getVal();
  snprintf(keyBuffer, keyBufferLength, "%02x%02x%02x%02x%02x%02x",
           addressBytes[5], addressBytes[4], addressBytes[3],
           addressBytes[2], addressBytes[1], addressBytes[0]);
}

static void storeLongTermKey(const NimBLEAddress &address, const uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  const size_t written = pairingStorage.putBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH);
  pairingStorage.end();

  Serial.printf("[PAIR]  LTK unter '%s' gespeichert (%u Byte).\n",
                storageKey, static_cast<unsigned>(written));
}

static bool loadLongTermKey(const NimBLEAddress &address, uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, true);
  const size_t read = pairingStorage.getBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH);
  pairingStorage.end();

  return read == PAIRING_KEY_LENGTH;
}

// --- Gespeicherte Kopplung nutzen -------------------------------------------
//
// ACHTUNG, das ist der einzige Teil dieser Firmware, der noch nicht am Geraet
// bestaetigt ist. Belegt ist nur: die Reconnect-Verbindung IST verschluesselt.
// In btle_joycon2_reconnect_encrypted.pcapng sind von 33 ATT-Zugriffen nur 16
// und von 709 Notifications nur 9 lesbar - der Rest liegt unter der
// Link-Layer-Verschluesselung.
//
// Nintendo handelt den Schluessel ueber die eigene Befehlsschnittstelle aus,
// nicht ueber SMP. NimBLE kennt diesen Weg nicht, also legen wir das Ergebnis
// von Hand in seinen Schluesselspeicher und stossen die Verschluesselung an.
//
// Nicht gepruefte Annahme: EDIV und Rand sind Null und das Verfahren zaehlt als
// Secure Connections. Das ist die uebliche Kodierung fuer einen Schluessel
// ohne Legacy-Aushandlung. Passt sie nicht, lehnt der Controller die
// Verschluesselung ab - sichtbar als sofortige Trennung nach dem Verbinden.
void applyStoredPairing(NimBLEClient *client, const NimBLEAddress &address)
{
  uint8_t longTermKey[PAIRING_KEY_LENGTH];
  if (!loadLongTermKey(address, longTermKey))
  {
    return;  // Nie gekoppelt - wie bisher unverschluesselt weiterarbeiten.
  }

  struct ble_store_value_sec securityValue;
  memset(&securityValue, 0, sizeof(securityValue));
  securityValue.peer_addr   = *address.getBase();
  securityValue.key_size    = PAIRING_KEY_LENGTH;
  securityValue.ediv        = 0;
  securityValue.rand_num    = 0;
  memcpy(securityValue.ltk, longTermKey, PAIRING_KEY_LENGTH);
  securityValue.ltk_present = 1;
  securityValue.sc          = 1;
  securityValue.authenticated = 0;

  // Beide Richtungen: der Host braucht den Schluessel als eigenen und als den
  // der Gegenstelle, sonst findet er ihn beim Verbinden nicht wieder.
  const int peerStatus = ble_store_write_peer_sec(&securityValue);
  const int ownStatus  = ble_store_write_our_sec(&securityValue);
  if (peerStatus != 0 || ownStatus != 0)
  {
    Serial.printf("[PAIR]  LTK konnte nicht hinterlegt werden (peer=%d, own=%d).\n",
                  peerStatus, ownStatus);
    return;
  }

  const int securityStatus = ble_gap_security_initiate(client->getConnHandle());
  Serial.printf("[PAIR]  Gespeicherte Kopplung genutzt, Verschluesselung angestossen (status=%d).\n",
                securityStatus);
}

// --- Pairing-Antrieb --------------------------------------------------------
//
// Das Protokoll selbst steckt in switch2_pairing.cpp. Hier wird nur getaktet:
// Anfrage senden, Antwort abholen, naechsten Schritt anstossen. Alles im
// Arduino-Task, weil AES und NVS im BLE-Callback nichts zu suchen haben.

static void sendNextPairingRequest(ControllerSession *session)
{
  uint8_t subcommandId = 0;
  uint8_t data[PAIRING_MAXIMUM_REQUEST_DATA_LENGTH] = {0};
  size_t  dataLength = 0;

  if (!buildPairingRequest(session->pairing, subcommandId, data, dataLength))
  {
    return;
  }

  if (!sendControllerCommand(session, COMMAND_ID_PAIRING, subcommandId, data, dataLength))
  {
    Serial.println("[PAIR]  Anfrage konnte nicht gesendet werden - Abbruch.");
    session->pairing.step = PairingStep::Failed;
  }
}

void startPairingForConnectedControllers()
{
  size_t startedCount = 0;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse || session->commandCharacteristic == nullptr)
    {
      continue;
    }

    startPairing(session->pairing, NimBLEDevice::getAddress().getVal());
    sendNextPairingRequest(session);
    startedCount++;
  }

  if (startedCount == 0)
  {
    Serial.println("[PAIR]  Kein verbundener Controller mit Befehlskanal - nichts zu tun.");
  }
  else
  {
    Serial.printf("[PAIR]  Handshake fuer %u Controller gestartet.\n",
                  static_cast<unsigned>(startedCount));
  }
}

void advancePairingSessions()
{
  const uint32_t nowMs = millis();

  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse)
    {
      continue;
    }

    if (session->hasCommandResponse)
    {
      session->hasCommandResponse = false;

      const uint8_t *response       = session->commandResponse;
      const size_t   responseLength = session->commandResponseLength;

      // Nur Pairing-Antworten treiben den Handshake. Alles andere ist bereits
      // als [CMD-RSP] protokolliert und hier nicht von Belang.
      if (responseLength > COMMAND_HEADER_LENGTH &&
          response[0] == COMMAND_ID_PAIRING &&
          response[1] == COMMAND_DIRECTION_DEVICE_TO_HOST)
      {
        const bool hasNextStep = handlePairingResponse(session->pairing,
                                                       response[3],
                                                       &response[COMMAND_HEADER_LENGTH],
                                                       responseLength - COMMAND_HEADER_LENGTH);
        if (hasNextStep)
        {
          sendNextPairingRequest(session);
        }
        else if (session->pairing.step == PairingStep::Completed)
        {
          storeLongTermKey(session->address, session->pairing.longTermKey);
          Serial.println("[PAIR]  Fertig. Der Controller sollte sich nach einem Neustart");
          Serial.println("[PAIR]  von selbst wieder melden - ohne Sync-Taste.");
        }
      }
    }

    checkPairingTimeout(session->pairing, nowMs);
  }
}
