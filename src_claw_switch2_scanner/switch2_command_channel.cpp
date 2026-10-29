#include "switch2_command_channel.h"

#include <Arduino.h>
#include <string.h>

#include "switch2_protocol.h"

bool sendControllerCommand(ControllerSession *session,
                           uint8_t            commandId,
                           uint8_t            subcommandId,
                           const uint8_t     *data,
                           size_t             dataLength)
{
  if (session == nullptr || session->commandCharacteristic == nullptr)
  {
    return false;
  }

  if (dataLength > MAXIMUM_COMMAND_DATA_LENGTH)
  {
    Serial.println("[CMD]   Befehlsdaten zu lang - nicht gesendet.");
    return false;
  }

  uint8_t packet[COMMAND_RUMBLE_PREFIX_LENGTH + COMMAND_HEADER_LENGTH +
                 MAXIMUM_COMMAND_DATA_LENGTH] = {0};

  uint8_t *header = &packet[COMMAND_RUMBLE_PREFIX_LENGTH];
  header[0] = commandId;
  header[1] = COMMAND_DIRECTION_HOST_TO_DEVICE;
  header[2] = COMMAND_TRANSPORT_BLUETOOTH;
  header[3] = subcommandId;
  header[4] = 0x00;
  header[5] = static_cast<uint8_t>(dataLength);
  header[6] = 0x00;
  header[7] = 0x00;

  if (data != nullptr && dataLength > 0)
  {
    memcpy(&header[COMMAND_HEADER_LENGTH], data, dataLength);
  }

  const size_t packetLength = COMMAND_RUMBLE_PREFIX_LENGTH + COMMAND_HEADER_LENGTH + dataLength;
  return session->commandCharacteristic->writeValue(packet, packetLength, false);
}

// --- Vibration --------------------------------------------------------------

static volatile bool         hasPendingVibration  = false;
static Switch2ControllerType pendingVibrationType = Switch2ControllerType::Unknown;

void requestVibration(Switch2ControllerType type)
{
  pendingVibrationType = type;
  hasPendingVibration  = true;
}

void sendPendingVibration()
{
  if (!hasPendingVibration)
  {
    return;
  }
  const Switch2ControllerType type = pendingVibrationType;
  hasPendingVibration              = false;

  ControllerSession *session = findSessionByType(type);
  if (session == nullptr || session->commandCharacteristic == nullptr)
  {
    Serial.println("[VIB]   Kein Befehlskanal fuer diesen Controller - nichts gesendet.");
    return;
  }

  // Sample-ID plus drei ungenutzte Bytes - exakt das Datenfeld, das die
  // Konsole im Mitschnitt schickt.
  const uint8_t vibrationData[VIBRATION_SAMPLE_DATA_LENGTH] = {
      VIBRATION_SAMPLE_FOR_BUTTON_FEEDBACK, 0x00, 0x00, 0x00};

  const bool didWrite = sendControllerCommand(session,
                                              COMMAND_ID_VIBRATION,
                                              VIBRATION_SUBCOMMAND_PLAY_SAMPLE,
                                              vibrationData,
                                              sizeof(vibrationData));

  // Der Name steht im Tracker, sobald der Report abonniert ist. Vor dem Abo
  // gibt es keinen - dann reicht die neutrale Bezeichnung.
  const char *controllerName = session->specificReportTracker.controllerName != nullptr
                                   ? session->specificReportTracker.controllerName
                                   : "Controller";

  Serial.printf("[VIB]   Muster 0x%02X an %s: %s\n",
                VIBRATION_SAMPLE_FOR_BUTTON_FEEDBACK,
                controllerName,
                didWrite ? "gesendet" : "FEHLGESCHLAGEN");
}

// --- Player-LEDs ------------------------------------------------------------

// Das LED-Muster ist der einzige Befehl, den diese Firmware unaufgefordert
// gleich nach dem Verbinden schickt. Seit er drin war, bricht die Verbindung
// nach 2-3 s mit einer Supervision-Timeout ab (reason=520) - und der
// Projektbericht hat die Verbindung vorher ausdruecklich als "stabil ueber
// 10 s" abgenommen (Abschnitt 8, Kriterium 2). Dass der Befehl ankommt und
// bleibenden Zustand aendert, ist am Geraet zu sehen: die LEDs blieben
// danach dunkel.
//
// Deshalb standardmaessig AUS. Die Verbindung hat Vorrang vor der Beleuchtung.
// 'l' setzt das Muster von Hand, 'L' schaltet die Automatik ein, falls sich
// der Verdacht erledigt.
static bool isAutomaticPlayerLedOn = false;

void setAutomaticPlayerLedEnabled(bool isEnabled)
{
  isAutomaticPlayerLedOn = isEnabled;
}

bool isAutomaticPlayerLedEnabled()
{
  return isAutomaticPlayerLedOn;
}

uint8_t playerLedPatternForType(Switch2ControllerType type)
{
  if (type == Switch2ControllerType::JoyConLeft)
  {
    return PLAYER_LED_PATTERN_JOYCON_LEFT;
  }
  if (type == Switch2ControllerType::JoyConRight)
  {
    return PLAYER_LED_PATTERN_JOYCON_RIGHT;
  }
  // Unbekannter Typ: alle vier an, damit ueberhaupt etwas Festes leuchtet.
  return 0x0F;
}

// Setzt NUR das Muster. Das Blinken wird hier bewusst nicht mitgeschaltet:
// vorher tat diese Funktion beides, und damit liess sich ein einmal
// abgeschaltetes Blinken mit keinem Kommando mehr zurueckholen - auch 'o'
// hat es jedes Mal wieder ausgeschaltet. Genau das war der Grund, warum ein
// Controller ohne jede Lichtreaktion dastand.
bool setPlayerLedPattern(ControllerSession *session, uint8_t ledBitmask)
{
  const uint8_t patternData[PLAYER_LED_DATA_LENGTH] = {ledBitmask};
  if (!sendControllerCommand(session,
                             COMMAND_ID_PLAYER_LED,
                             PLAYER_LED_SUBCOMMAND_SET_PATTERN,
                             patternData,
                             sizeof(patternData)))
  {
    Serial.println("[LED]   Muster konnte nicht gesetzt werden.");
    return false;
  }

  Serial.printf("[LED]   Muster 0x%02X gesetzt.\n", static_cast<unsigned>(ledBitmask));
  return true;
}

// Blinken des aktuellen Musters an oder aus (Command 0x09/0x08). Das
// Gegenstueck zum Abschalten - und die einzige Moeglichkeit, es
// zurueckzuholen.
bool setPlayerLedFlashing(ControllerSession *session, bool shouldFlash)
{
  // Zwei Befehle unmittelbar hintereinander koennten den Controller
  // ueberfahren - die Konsole schickt sie im Mitschnitt auch nicht in
  // derselben Millisekunde. Ein kurzer Abstand kostet nichts.
  delay(20);

  const uint8_t flashData[PLAYER_LED_DATA_LENGTH] = {shouldFlash ? uint8_t{0x01} : uint8_t{0x00}};
  const bool    didWrite = sendControllerCommand(session,
                                                 COMMAND_ID_PLAYER_LED,
                                                 PLAYER_LED_SUBCOMMAND_FLASH,
                                                 flashData,
                                                 sizeof(flashData));
  Serial.printf("[LED]   Blinken %s: %s\n",
                shouldFlash ? "an" : "aus",
                didWrite ? "gesendet" : "FEHLGESCHLAGEN");
  return didWrite;
}

// Setzt bei allen verbundenen Controllern ein Muster. ledBitmask = 0 bedeutet
// "jeder bekommt sein eigenes" (links die erste LED, rechts die zweite).
static void setPlayerLedsForConnectedControllers(uint8_t ledBitmask)
{
  size_t setCount = 0;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse || session->commandCharacteristic == nullptr)
    {
      continue;
    }

    const uint8_t pattern = ledBitmask != 0 ? ledBitmask : playerLedPatternForType(session->type);
    if (setPlayerLedPattern(session, pattern))
    {
      // Festes Muster heisst: nicht blinken, sonst sieht es nach
      // Pairing-Modus aus.
      setPlayerLedFlashing(session, false);
      setCount++;
    }
  }

  if (setCount == 0)
  {
    Serial.println("[LED]   Kein verbundener Controller mit Befehlskanal - nichts zu tun.");
  }
}

void setPlayerLedsForConnectedControllers()
{
  setPlayerLedsForConnectedControllers(0);
}

// Alle vier LEDs an. Zweierlei Zweck: die Leuchten eines Controllers
// zurueckholen, bei dem ein frueher gesetztes Muster haengt, und pruefen, ob
// der LED-Befehl ueberhaupt greift - vier leuchtende LEDs sind nicht zu
// uebersehen.
//
// Dunkle LEDs an einem GETRENNTEN Joy-Con muessen uebrigens nicht an uns
// liegen: ein Joy-Con ohne Verbindung schlaft ein und schaltet sie selbst ab.
void turnOnAllPlayerLeds()
{
  size_t setCount = 0;
  for (size_t i = 0; i < MAXIMUM_CONTROLLER_SESSIONS; i++)
  {
    ControllerSession *session = &controllerSessions[i];
    if (!session->isInUse || session->commandCharacteristic == nullptr)
    {
      continue;
    }

    if (setPlayerLedPattern(session, PLAYER_LED_PATTERN_ALL))
    {
      // Hier ausdruecklich MIT Blinken: dieser Befehl ist der Rueckweg aus
      // einem Controller, dem wir die Lichtrueckmeldung abgeschaltet haben.
      setPlayerLedFlashing(session, true);
      setCount++;
    }
  }

  if (setCount == 0)
  {
    Serial.println("[LED]   Kein verbundener Controller mit Befehlskanal - nichts zu tun.");
  }
}

// --- Kopplungsspeicher des Controllers --------------------------------------

bool clearControllerPairingInfo(ControllerSession *session)
{
  const bool didWrite = sendControllerCommand(session,
                                              COMMAND_ID_INITIALISATION,
                                              INITIALISATION_SUBCOMMAND_CLEAR_PAIRING_INFO,
                                              nullptr,
                                              0);
  Serial.printf("[PAIR]  Kopplung im Controller verwerfen: %s\n",
                didWrite ? "gesendet" : "FEHLGESCHLAGEN");
  return didWrite;
}
