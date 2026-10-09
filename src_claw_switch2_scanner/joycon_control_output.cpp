#include "joycon_control_output.h"

#include <Arduino.h>
#include <stdio.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

static constexpr const char *JOYCON_CONTROL_TOPIC = "clawmachine/player_input/joycon";

static ClawMqttConnection mqttConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    "claw_switch2_scanner",
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

// Je Controller ein eigener Zustand - zusammengefuehrt wird erst beim
// Publizieren.
static JoyConControlState leftControlState;
static JoyConControlState rightControlState;
static volatile bool      hasPendingControlState = false;

void beginControlOutput()
{
  Serial.printf("[MQTT]  Broker %s:%d, Topic %s\n",
                CLAW_MQTT_BROKER_HOST, CLAW_MQTT_BROKER_PORT, JOYCON_CONTROL_TOPIC);
  mqttConnection.begin();
}

void maintainControlOutput()
{
  mqttConnection.maintainConnection();
}

void storeControlState(Switch2ControllerType type, const JoyConControlState &state)
{
  if (type == Switch2ControllerType::JoyConLeft)
  {
    leftControlState = state;
  }
  else if (type == Switch2ControllerType::JoyConRight)
  {
    rightControlState = state;
  }
  else
  {
    return;
  }
  hasPendingControlState = true;
}

void clearControlState(Switch2ControllerType type)
{
  if (type == Switch2ControllerType::JoyConLeft)
  {
    leftControlState = JoyConControlState();
  }
  else if (type == Switch2ControllerType::JoyConRight)
  {
    rightControlState = JoyConControlState();
  }
  else
  {
    return;
  }
  hasPendingControlState = true;
}

// Beide Controller verodern: was auf einer der beiden Seiten gedrueckt ist,
// gilt. So funktioniert gleichzeitiges Bedienen sinnvoll.
static JoyConControlState mergeControlStates()
{
  JoyConControlState state;
  state.upButton    = leftControlState.upButton    || rightControlState.upButton;
  state.downButton  = leftControlState.downButton  || rightControlState.downButton;
  state.leftButton  = leftControlState.leftButton  || rightControlState.leftButton;
  state.rightButton = leftControlState.rightButton || rightControlState.rightButton;
  state.frontButton = leftControlState.frontButton || rightControlState.frontButton;
  state.backButton  = leftControlState.backButton  || rightControlState.backButton;
  return state;
}

void publishPendingControlState()
{
  if (!hasPendingControlState)
  {
    return;
  }

  hasPendingControlState = false;

  const JoyConControlState state = mergeControlStates();

  if (!state.isValid())
  {
    return;
  }

  // Nur bei Zustandswechsel senden. Bei rund 30 Reports pro Sekunde waere
  // zyklisches Publizieren eine Flut - und es entlastet die Antenne, die sich
  // WiFi und BLE teilen.
  static JoyConControlState lastPublishedState;
  static bool               hasPublishedOnce = false;

  if (hasPublishedOnce && state.equals(lastPublishedState))
  {
    return;
  }

  char payload[160];
  snprintf(payload, sizeof(payload),
           "{\"up\":%d,\"down\":%d,\"left\":%d,\"right\":%d,\"front\":%d,\"back\":%d}",
           state.upButton, state.downButton,
           state.leftButton, state.rightButton,
           state.frontButton, state.backButton);

  mqttConnection.publish(JOYCON_CONTROL_TOPIC, payload);
  Serial.printf("[MQTT]  %s -> %s\n", JOYCON_CONTROL_TOPIC, payload);

  lastPublishedState = state;
  hasPublishedOnce   = true;
}
