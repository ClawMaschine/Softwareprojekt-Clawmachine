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
