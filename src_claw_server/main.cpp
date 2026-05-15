#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

ClawMqttConnection controlPanelConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_CONTROL_PANEL_CLIENT_ID,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[CONTROL_PANEL] MQTT client starts");
  Serial.print("[CONTROL_PANEL] Client ID: ");
  Serial.println(CLAW_CONTROL_PANEL_CLIENT_ID);
  controlPanelConnection.begin();
}
