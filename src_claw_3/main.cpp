#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

ClawMqttConnection motorControllerConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_MOTOR_CONTROLLER_CLIENT_ID,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[MOTOR_CONTROLLER] MQTT client starts");
  Serial.print("[MOTOR_CONTROLLER] Client ID: ");
  Serial.println(xxx);
  motorControllerConnection.begin();
}

void loop()
{
  motorControllerConnection.maintainConnection();
  delay(20);
}
