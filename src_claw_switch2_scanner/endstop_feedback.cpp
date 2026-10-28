#include "endstop_feedback.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "switch2_command_channel.h"

static constexpr const char *ENDSTOP_STATE_TOPIC   = "clawmachine/endstop/state";
static constexpr const char *ENDSTOP_FIELD_NAMES[] = {"x1", "x2", "y1", "y2", "z1"};
static constexpr size_t      ENDSTOP_FIELD_COUNT =
    sizeof(ENDSTOP_FIELD_NAMES) / sizeof(ENDSTOP_FIELD_NAMES[0]);

const char *endstopStateTopic()
{
  return ENDSTOP_STATE_TOPIC;
}

void handleEndstopStateMessage(const uint8_t *payload, size_t length)
{
  JsonDocument jsonDocument;
  if (deserializeJson(jsonDocument, payload, length))
  {
    return;
  }

  static bool hasEndstopBeenTriggered[ENDSTOP_FIELD_COUNT] = {};
  // Die erste Meldung nach dem Start setzt nur den Ausgangszustand: ein schon
  // gedrueckter Endstop soll nicht beim Verbinden vibrieren.
  static bool hasReceivedFirstState = false;

  bool hasNewlyTriggeredEndstop = false;
  for (size_t i = 0; i < ENDSTOP_FIELD_COUNT; i++)
  {
    if (jsonDocument[ENDSTOP_FIELD_NAMES[i]].isNull())
    {
      continue;
    }
    const bool isTriggered = jsonDocument[ENDSTOP_FIELD_NAMES[i]].as<int>() == 1;
    if (isTriggered && !hasEndstopBeenTriggered[i])
    {
      hasNewlyTriggeredEndstop = true;
      Serial.printf("[ENDSTOP] %s ausgeloest\n", ENDSTOP_FIELD_NAMES[i]);
    }
    hasEndstopBeenTriggered[i] = isTriggered;
  }

  if (hasNewlyTriggeredEndstop && hasReceivedFirstState)
  {
    requestVibration(Switch2ControllerType::JoyConLeft);
    requestVibration(Switch2ControllerType::JoyConRight);
  }
  hasReceivedFirstState = true;
}
