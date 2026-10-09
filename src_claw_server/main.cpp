#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

static constexpr const char *ENDSTOP_TOPIC = "clawmachine/endstop/state";

static constexpr uint8_t endstopXOnePin = CLAW_ENDSTOP_X_1;
static constexpr uint8_t endstopXTwoPin = CLAW_ENDSTOP_X_2;
static constexpr uint8_t endstopYOnePin = CLAW_ENDSTOP_Y_1;

ClawMqttConnection endstopConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_ENDSTOP_CLIENT_ID,
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

char lastPublishedEndstopPayload[96] = "";

void publishEndstopStateIfChanged()
{
  // Endstops liegen per INPUT_PULLUP auf HIGH und werden beim Ausloesen gegen
  // GND gezogen -> LOW heisst ausgeloest.
  const bool isEndstopXOneTriggered = digitalRead(endstopXOnePin) == LOW;
  const bool isEndstopXTwoTriggered = digitalRead(endstopXTwoPin) == LOW;
  const bool isEndstopYOneTriggered = digitalRead(endstopYOnePin) == LOW;

  char payload[96];
  snprintf(
      payload,
      sizeof(payload),
      "{\"x1\":%d,\"x2\":%d,\"y1\":%d}",
      isEndstopXOneTriggered,
      isEndstopXTwoTriggered,
      isEndstopYOneTriggered);

  if (strcmp(payload, lastPublishedEndstopPayload) == 0)
  {
    return;
  }

  endstopConnection.publish(ENDSTOP_TOPIC, payload);
  Serial.printf("[ENDSTOP] Published: %s\n", payload);

  strncpy(lastPublishedEndstopPayload, payload, sizeof(lastPublishedEndstopPayload) - 1);
  lastPublishedEndstopPayload[sizeof(lastPublishedEndstopPayload) - 1] = '\0';
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[ENDSTOP] MQTT client starts");
  Serial.print("[ENDSTOP] Client ID: ");
  Serial.println(CLAW_ENDSTOP_CLIENT_ID);

  pinMode(endstopXOnePin, INPUT_PULLUP);
  pinMode(endstopXTwoPin, INPUT_PULLUP);
  pinMode(endstopYOnePin, INPUT_PULLUP);

  endstopConnection.begin();
}

void loop()
{
  endstopConnection.maintainConnection();
  publishEndstopStateIfChanged();
  delay(20);
}
