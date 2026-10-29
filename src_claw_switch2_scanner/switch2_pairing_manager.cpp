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

// Die Arduino-Preferences-API kann Schluessel nicht aufzaehlen. Damit sich
// nachsehen und loeschen laesst, was gespeichert ist, wird die Liste der
// gekoppelten Adressen unter einem festen Schluessel mitgefuehrt - als rohe
// 6-Byte-Adressen hintereinander. Vier Plaetze reichen fuer zwei Joy-Cons mit
// Luft; der Name beginnt mit einem Unterstrich, damit er nie mit einer
// Adresshexkette kollidiert.
static constexpr const char *PAIRED_ADDRESS_LIST_KEY   = "_paired";
static constexpr size_t      MAXIMUM_STORED_PAIRINGS   = 4;

// Ein nur lesender Preferences-Zugriff auf einen Namensraum, den es noch nicht
// gibt, scheitert mit "nvs_open failed: NOT_FOUND" - und die Arduino-Huelle
// protokolliert das selbst als Fehler. Da jeder Scan eines fremd gekoppelten
// Controllers nachsieht, ob wir ihn kennen, stand diese Zeile sonst dauernd im
// Log und sah nach einem Defekt aus. Einmal schreibend oeffnen legt den
// Namensraum an, danach ist Ruhe.
void beginPairingStorage()
{
  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  pairingStorage.end();
}

// NVS-Schluessel duerfen hoechstens 15 Zeichen lang sein. Die Adresse als
// Hexkette ohne Trenner braucht genau 12.
static void buildStorageKeyFromBytes(const uint8_t *addressBytes,
                                     char          *keyBuffer,
                                     size_t         keyBufferLength)
{
  snprintf(keyBuffer, keyBufferLength, "%02x%02x%02x%02x%02x%02x",
           addressBytes[5], addressBytes[4], addressBytes[3],
           addressBytes[2], addressBytes[1], addressBytes[0]);
}

static void buildStorageKey(const NimBLEAddress &address, char *keyBuffer, size_t keyBufferLength)
{
  buildStorageKeyFromBytes(address.getVal(), keyBuffer, keyBufferLength);
}

// Anzahl der gelesenen Adressen. buffer muss MAXIMUM_STORED_PAIRINGS Adressen
// aufnehmen koennen.
static size_t loadPairedAddressList(uint8_t *buffer)
{
  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, true);
  // isKey() zuerst, aus demselben Grund wie in loadLongTermKey: auf einem
  // frischen Board gibt es die Liste noch nicht, und getBytes() wuerde das
  // als Fehler protokollieren.
  const size_t readBytes =
      pairingStorage.isKey(PAIRED_ADDRESS_LIST_KEY)
          ? pairingStorage.getBytes(PAIRED_ADDRESS_LIST_KEY, buffer,
                                    MAXIMUM_STORED_PAIRINGS * BLUETOOTH_ADDRESS_LENGTH)
          : 0;
  pairingStorage.end();

  return readBytes / BLUETOOTH_ADDRESS_LENGTH;
}

// Haelt die Adressliste aktuell. Eine schon gelistete Adresse wird nicht
// doppelt eingetragen, damit erneutes Koppeln die Liste nicht fuellt.
static void rememberPairedAddress(const NimBLEAddress &address)
{
  uint8_t addressList[MAXIMUM_STORED_PAIRINGS * BLUETOOTH_ADDRESS_LENGTH] = {0};
  size_t  addressCount = loadPairedAddressList(addressList);

  for (size_t i = 0; i < addressCount; i++)
  {
    if (memcmp(&addressList[i * BLUETOOTH_ADDRESS_LENGTH], address.getVal(),
               BLUETOOTH_ADDRESS_LENGTH) == 0)
    {
      return;
    }
  }

  if (addressCount >= MAXIMUM_STORED_PAIRINGS)
  {
    // Aeltesten Eintrag verdraengen: der Platz ist begrenzt, und die zuletzt
    // gekoppelten Controller sind die, die gebraucht werden.
    memmove(addressList, &addressList[BLUETOOTH_ADDRESS_LENGTH],
            (MAXIMUM_STORED_PAIRINGS - 1) * BLUETOOTH_ADDRESS_LENGTH);
    addressCount = MAXIMUM_STORED_PAIRINGS - 1;
  }

  memcpy(&addressList[addressCount * BLUETOOTH_ADDRESS_LENGTH], address.getVal(),
         BLUETOOTH_ADDRESS_LENGTH);
  addressCount++;

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  pairingStorage.putBytes(PAIRED_ADDRESS_LIST_KEY, addressList,
                          addressCount * BLUETOOTH_ADDRESS_LENGTH);
  pairingStorage.end();
}

static void storeLongTermKey(const NimBLEAddress &address, const uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  const size_t written = pairingStorage.putBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH);
  pairingStorage.end();

  rememberPairedAddress(address);

  Serial.printf("[PAIR]  LTK unter '%s' gespeichert (%u Byte).\n",
                storageKey, static_cast<unsigned>(written));
}

static bool loadLongTermKey(const NimBLEAddress &address, uint8_t *longTermKey)
{
  char storageKey[16];
  buildStorageKey(address, storageKey, sizeof(storageKey));

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, true);
  // Erst fragen, ob der Schluessel ueberhaupt existiert. getBytes() auf einen
  // fehlenden Schluessel protokolliert die Arduino-Huelle selbst als Fehler
  // ("nvs_get_blob len fail ... NOT_FOUND"), und weil bei jedem Scan eines
  // fremd gekoppelten Controllers nachgesehen wird, stand diese Zeile sonst
  // dauernd im Log und sah nach einem Defekt aus. isKey() ist still.
  const bool   hasKey = pairingStorage.isKey(storageKey);
  const size_t read   = hasKey ? pairingStorage.getBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH) : 0;
  pairingStorage.end();

  return read == PAIRING_KEY_LENGTH;
}

// --- Kopplungen abfragen, anzeigen, loeschen --------------------------------

bool hasStoredPairing(const NimBLEAddress &address)
{
  uint8_t longTermKey[PAIRING_KEY_LENGTH];
  return loadLongTermKey(address, longTermKey);
}

void printStoredPairings()
{
  uint8_t      addressList[MAXIMUM_STORED_PAIRINGS * BLUETOOTH_ADDRESS_LENGTH] = {0};
  const size_t addressCount = loadPairedAddressList(addressList);

  if (addressCount == 0)
  {
    Serial.println("[PAIR]  Keine gespeicherte Kopplung. Sync-Taste lang druecken,");
    Serial.println("[PAIR]  verbinden lassen und dann 'p' druecken.");
    return;
  }

  Serial.printf("[PAIR]  %u gespeicherte Kopplung(en):\n", static_cast<unsigned>(addressCount));
  for (size_t i = 0; i < addressCount; i++)
  {
    const uint8_t *addressBytes = &addressList[i * BLUETOOTH_ADDRESS_LENGTH];

    char storageKey[16];
    buildStorageKeyFromBytes(addressBytes, storageKey, sizeof(storageKey));

    uint8_t      longTermKey[PAIRING_KEY_LENGTH];
    pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, true);
    // Auch hier isKey() zuerst: genau der Fall "Adresse gelistet, LTK fehlt"
    // wird unten gemeldet, und er soll als Hinweis erscheinen und nicht als
    // Fehlerzeile der Preferences-Huelle.
    const size_t keyLength =
        pairingStorage.isKey(storageKey)
            ? pairingStorage.getBytes(storageKey, longTermKey, PAIRING_KEY_LENGTH)
            : 0;
    pairingStorage.end();

    Serial.printf("[PAIR]    %02x:%02x:%02x:%02x:%02x:%02x  LTK %s\n",
                  addressBytes[5], addressBytes[4], addressBytes[3],
                  addressBytes[2], addressBytes[1], addressBytes[0],
                  keyLength == PAIRING_KEY_LENGTH ? "vorhanden" : "FEHLT");
  }
  Serial.println("[PAIR]  Diese Controller sollten sich auf einen Tastendruck melden.");

  // Der zweite Speicher: applyStoredPairing legt den LTK zusaetzlich in den
  // Schluesselspeicher von NimBLE. Ohne diese Zeile waere der unsichtbar -
  // und eine Abweichung zwischen beiden Zahlen ist genau die Spur, die man
  // bei einem Reconnect sucht, der nicht klappt.
  Serial.printf("[PAIR]  NimBLE haelt %d Bond(s) im eigenen Schluesselspeicher.\n",
                NimBLEDevice::getNumBonds());
}

void clearStoredPairings()
{
  // Zuerst den Controllern sagen, dass sie uns vergessen sollen - danach ist
  // der Befehlskanal vielleicht nicht mehr erreichbar.
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (session->isInUse && session->commandCharacteristic != nullptr)
    {
      clearControllerPairingInfo(session);
    }
  }

  uint8_t      addressList[MAXIMUM_STORED_PAIRINGS * BLUETOOTH_ADDRESS_LENGTH] = {0};
  const size_t addressCount = loadPairedAddressList(addressList);

  pairingStorage.begin(PAIRING_STORAGE_NAMESPACE, false);
  for (size_t i = 0; i < addressCount; i++)
  {
    char storageKey[16];
    buildStorageKeyFromBytes(&addressList[i * BLUETOOTH_ADDRESS_LENGTH],
                             storageKey, sizeof(storageKey));
    pairingStorage.remove(storageKey);
  }
  pairingStorage.remove(PAIRED_ADDRESS_LIST_KEY);
  pairingStorage.end();

  Serial.printf("[PAIR]  %u Kopplung(en) aus unserem Speicher geloescht.\n",
                static_cast<unsigned>(addressCount));

  // Die wichtigste Stelle: applyStoredPairing schreibt den LTK ueber
  // ble_store_write_peer_sec/our_sec in den Schluesselspeicher von NimBLE.
  // Bleibt er dort stehen, versucht NimBLE bei einer spaeteren Verbindung mit
  // einem veralteten Schluessel zu verschluesseln - und der Controller trennt.
  // Unseren Namensraum zu leeren hat ohne das nur halb gewirkt.
  const int bondCount = NimBLEDevice::getNumBonds();
  if (NimBLEDevice::deleteAllBonds())
  {
    Serial.printf("[PAIR]  %d NimBLE-Bond(s) geloescht.\n", bondCount);
  }
  else
  {
    Serial.println("[PAIR]  ACHTUNG: NimBLE-Bonds konnten nicht geloescht werden.");
  }

  Serial.println("[PAIR]  Zum Wiederverbinden ist jetzt wieder der lange Sync-Druck");
  Serial.println("[PAIR]  und 'p' noetig.");
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
bool applyStoredPairing(NimBLEClient *client, const NimBLEAddress &address)
{
  uint8_t longTermKey[PAIRING_KEY_LENGTH];
  if (!loadLongTermKey(address, longTermKey))
  {
    return false;  // Nie gekoppelt - wie bisher unverschluesselt weiterarbeiten.
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
    return false;
  }

  const int securityStatus = ble_gap_security_initiate(client->getConnHandle());
  Serial.printf("[PAIR]  Gespeicherte Kopplung genutzt, Verschluesselung angestossen (status=%d).\n",
                securityStatus);
  return securityStatus == 0;
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

    // Uhr hier ablesen und nicht am Schleifenanfang: zwischendurch kann eine
    // Antwort verarbeitet (AES!) und die naechste Anfrage gesendet worden
    // sein. Mit einer aelteren Uhrzeit laege die Anfrage in der Zukunft und
    // der Schritt waere sofort "abgelaufen".
    checkPairingTimeout(session->pairing, millis());
  }
}
