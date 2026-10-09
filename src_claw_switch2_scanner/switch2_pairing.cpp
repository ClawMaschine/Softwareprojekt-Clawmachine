#include "switch2_pairing.h"

#include <Arduino.h>
#include <string.h>

#include "esp_random.h"
#include "mbedtls/aes.h"

// Subcommands des Pairing-Befehls 0x15.
static constexpr uint8_t PAIRING_SUBCOMMAND_HOST_ADDRESSES = 0x01;
static constexpr uint8_t PAIRING_SUBCOMMAND_CHALLENGE      = 0x02;
static constexpr uint8_t PAIRING_SUBCOMMAND_FINALIZE       = 0x03;
static constexpr uint8_t PAIRING_SUBCOMMAND_PUBLIC_KEYS    = 0x04;

// Erstes Datenbyte: 0x00 in Anfragen, 0x01 in Antworten. Im Mitschnitt ist das
// bei allen vier Schritten so.
static constexpr uint8_t PAIRING_REQUEST_MARKER  = 0x00;
static constexpr uint8_t PAIRING_RESPONSE_MARKER = 0x01;

// Die Konsole meldet zwei Host-Adressen. Wir haben nur eine und schicken sie
// doppelt: der Controller speichert beide und advertised spaeter mit einer
// davon - so passt in jedem Fall unsere.
static constexpr uint8_t PAIRING_HOST_ADDRESS_COUNT = 2;

static void fillWithRandomBytes(uint8_t *buffer, size_t length)
{
  for (size_t i = 0; i < length; i += 4)
  {
    const uint32_t randomWord = esp_random();
    const size_t   remaining  = length - i;
    const size_t   chunk      = remaining < 4 ? remaining : 4;
    memcpy(&buffer[i], &randomWord, chunk);
  }
}

static void reverseBytes(uint8_t *destination, const uint8_t *source, size_t length)
{
  for (size_t i = 0; i < length; i++)
  {
    destination[i] = source[length - 1 - i];
  }
}

// B2 = AES-128-ECB(Schluessel = reverse(LTK), Klartext = reverse(A2)).
// Beide Umkehrungen sind noetig - an den Bytes aus dem Mitschnitt geprueft.
static bool computeExpectedChallengeResponse(const uint8_t *longTermKey,
                                             const uint8_t *challenge,
                                             uint8_t       *expectedResponse)
{
  uint8_t reversedKey[PAIRING_KEY_LENGTH];
  uint8_t reversedChallenge[PAIRING_KEY_LENGTH];
  reverseBytes(reversedKey, longTermKey, PAIRING_KEY_LENGTH);
  reverseBytes(reversedChallenge, challenge, PAIRING_KEY_LENGTH);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  bool didSucceed = mbedtls_aes_setkey_enc(&aes, reversedKey, 128) == 0;
  if (didSucceed)
  {
    didSucceed = mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT,
                                       reversedChallenge, expectedResponse) == 0;
  }

  mbedtls_aes_free(&aes);
  return didSucceed;
}

void startPairing(Switch2PairingSession &session, const uint8_t *hostAddress)
{
  memset(&session, 0, sizeof(session));
  memcpy(session.hostAddress, hostAddress, BLUETOOTH_ADDRESS_LENGTH);
  session.step = PairingStep::Idle;
}

bool buildPairingRequest(Switch2PairingSession &session,
                         uint8_t              &subcommandId,
                         uint8_t              *data,
                         size_t               &dataLength)
{
  data[0] = PAIRING_REQUEST_MARKER;

  switch (session.step)
  {
    case PairingStep::Idle:
      // Schritt 1: beide Host-Adressen. Little Endian, wie NimBLE sie fuehrt.
      subcommandId = PAIRING_SUBCOMMAND_HOST_ADDRESSES;
      data[1]      = PAIRING_HOST_ADDRESS_COUNT;
      memcpy(&data[2], session.hostAddress, BLUETOOTH_ADDRESS_LENGTH);
      memcpy(&data[2 + BLUETOOTH_ADDRESS_LENGTH], session.hostAddress, BLUETOOTH_ADDRESS_LENGTH);
      dataLength   = 2 + 2 * BLUETOOTH_ADDRESS_LENGTH;
      session.step = PairingStep::HostAddresses;
      break;

    case PairingStep::HostAddresses:
      // Schritt 2: eigener Public Key. Jeder Durchlauf wuerfelt neu, damit
      // nicht zweimal derselbe LTK entsteht.
      subcommandId = PAIRING_SUBCOMMAND_PUBLIC_KEYS;
      fillWithRandomBytes(session.hostPublicKey, PAIRING_KEY_LENGTH);
      memcpy(&data[1], session.hostPublicKey, PAIRING_KEY_LENGTH);
      dataLength   = 1 + PAIRING_KEY_LENGTH;
      session.step = PairingStep::PublicKeys;
      break;

    case PairingStep::PublicKeys:
      // Schritt 3: Challenge. Der Controller beweist damit, dass er denselben
      // LTK berechnet hat.
      subcommandId = PAIRING_SUBCOMMAND_CHALLENGE;
      fillWithRandomBytes(session.hostChallenge, PAIRING_KEY_LENGTH);
      memcpy(&data[1], session.hostChallenge, PAIRING_KEY_LENGTH);
      dataLength   = 1 + PAIRING_KEY_LENGTH;
      session.step = PairingStep::Challenge;
      break;

    case PairingStep::Challenge:
      // Schritt 4: bestaetigen. Erst hier schreibt der Controller Adresse und
      // LTK in seinen Speicher.
      subcommandId = PAIRING_SUBCOMMAND_FINALIZE;
      dataLength   = 1;
      session.step = PairingStep::Finalize;
      break;

    default:
      return false;
  }

  session.lastRequestAtMs = millis();
  return true;
}

bool handlePairingResponse(Switch2PairingSession &session,
                           uint8_t               subcommandId,
                           const uint8_t        *responseData,
                           size_t                length)
{
  if (length < 1 || responseData[0] != PAIRING_RESPONSE_MARKER)
  {
    Serial.println("[PAIR]  Antwort ohne erwartetes Statusbyte 0x01 - Abbruch.");
    session.step = PairingStep::Failed;
    return false;
  }

  switch (session.step)
  {
    case PairingStep::HostAddresses:
    {
      if (subcommandId != PAIRING_SUBCOMMAND_HOST_ADDRESSES)
      {
        break;
      }
      // Die letzten sechs Bytes sind die Adresse des Controllers. Die davor
      // liegenden Felder sind nicht entschluesselt und werden nicht gebraucht.
      if (length >= 1 + BLUETOOTH_ADDRESS_LENGTH)
      {
        memcpy(session.controllerAddress,
               &responseData[length - BLUETOOTH_ADDRESS_LENGTH],
               BLUETOOTH_ADDRESS_LENGTH);
        session.hasControllerAddress = true;
      }
      Serial.println("[PAIR]  Schritt 1/4: Adressen ausgetauscht.");
      return true;
    }

    case PairingStep::PublicKeys:
    {
      if (subcommandId != PAIRING_SUBCOMMAND_PUBLIC_KEYS ||
          length < 1 + PAIRING_KEY_LENGTH)
      {
        break;
      }
      const uint8_t *controllerPublicKey = &responseData[1];
      for (size_t i = 0; i < PAIRING_KEY_LENGTH; i++)
      {
        session.longTermKey[i] = session.hostPublicKey[i] ^ controllerPublicKey[i];
      }
      session.hasLongTermKey = true;
      Serial.println("[PAIR]  Schritt 2/4: Schluessel getauscht, LTK berechnet.");
      return true;
    }

    case PairingStep::Challenge:
    {
      if (subcommandId != PAIRING_SUBCOMMAND_CHALLENGE ||
          length < 1 + PAIRING_KEY_LENGTH)
      {
        break;
      }

      uint8_t expectedResponse[PAIRING_KEY_LENGTH];
      if (!computeExpectedChallengeResponse(session.longTermKey,
                                            session.hostChallenge,
                                            expectedResponse))
      {
        Serial.println("[PAIR]  AES-Berechnung fehlgeschlagen - Abbruch.");
        session.step = PairingStep::Failed;
        return false;
      }

      if (memcmp(expectedResponse, &responseData[1], PAIRING_KEY_LENGTH) != 0)
      {
        // Kein Abschluss ohne Beweis: sonst wuerde der Controller einen
        // Schluessel speichern, mit dem wir spaeter nicht verschluesseln
        // koennen, und waere fuer uns unbrauchbar gekoppelt.
        Serial.println("[PAIR]  Challenge stimmt NICHT - kein Abschluss, Abbruch.");
        session.step = PairingStep::Failed;
        return false;
      }

      Serial.println("[PAIR]  Schritt 3/4: Challenge bestaetigt, LTK ist beidseitig gleich.");
      return true;
    }

    case PairingStep::Finalize:
    {
      if (subcommandId != PAIRING_SUBCOMMAND_FINALIZE)
      {
        break;
      }
      session.step = PairingStep::Completed;
      Serial.println("[PAIR]  Schritt 4/4: Controller hat Host und LTK gespeichert.");
      if (session.hasControllerAddress)
      {
        // Zur Gegenprobe: diese Adresse muss der Verbindungsadresse
        // entsprechen, unter der der LTK gleich abgelegt wird.
        Serial.printf("[PAIR]  Controller meldet Adresse %02x:%02x:%02x:%02x:%02x:%02x\n",
                      session.controllerAddress[5], session.controllerAddress[4],
                      session.controllerAddress[3], session.controllerAddress[2],
                      session.controllerAddress[1], session.controllerAddress[0]);
      }
      return false;
    }

    default:
      break;
  }

  Serial.printf("[PAIR]  Unerwartete Antwort (Subcommand 0x%02X in Schritt %s) - Abbruch.\n",
                subcommandId, describePairingStep(session.step));
  session.step = PairingStep::Failed;
  return false;
}

bool checkPairingTimeout(Switch2PairingSession &session, uint32_t nowMs)
{
  const bool isWaiting = session.step == PairingStep::HostAddresses ||
                         session.step == PairingStep::PublicKeys ||
                         session.step == PairingStep::Challenge ||
                         session.step == PairingStep::Finalize;
  if (!isWaiting)
  {
    return false;
  }
  if ((nowMs - session.lastRequestAtMs) < PAIRING_RESPONSE_TIMEOUT_MS)
  {
    return false;
  }

  Serial.printf("[PAIR]  Keine Antwort in Schritt %s - Abbruch.\n",
                describePairingStep(session.step));
  session.step = PairingStep::Failed;
  return true;
}

const char *describePairingStep(PairingStep step)
{
  switch (step)
  {
    case PairingStep::Idle:          return "Idle";
    case PairingStep::HostAddresses: return "Adressen (0x01)";
    case PairingStep::PublicKeys:    return "Schluessel (0x04)";
    case PairingStep::Challenge:     return "Challenge (0x02)";
    case PairingStep::Finalize:      return "Abschluss (0x03)";
    case PairingStep::Completed:     return "Fertig";
    case PairingStep::Failed:        return "Fehlgeschlagen";
  }
  return "unbekannt";
}
