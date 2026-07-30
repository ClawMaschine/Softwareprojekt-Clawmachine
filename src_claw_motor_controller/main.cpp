#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "claw_motor_controller.h"
#include "firmware_config.h"
#include <ArduinoJson.h>

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);

ClawMqttConnection motorControllerConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_MOTOR_CONTROLLER_CLIENT_ID,
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

ClawMotorController movementController(
    motorControllerConnection,
    CLAW_MOTOR_SHIELD_A_I2C_ADDRESS,
    CLAW_MOTOR_SHIELD_B_I2C_ADDRESS,
    CLAW_MOTOR_MAX_REVOLUTIONS_PER_MINUTE,
    CLAW_CLAW_SERVO_PIN);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[MOTOR_CONTROLLER] MQTT client starts");
  Serial.print("[MOTOR_CONTROLLER] Client ID: ");
  Serial.println(CLAW_MOTOR_CONTROLLER_CLIENT_ID);
  motorControllerConnection.begin();
  movementController.begin();
  motorControllerConnection.setMessageCallback(onMqttMessage);
  motorControllerConnection.begin();
  motorControllerConnection.subscribe("clawmachine/motor_controller/command");
}

void loop()
{
  motorControllerConnection.maintainConnection();
  movementController.update();

  String payload = "Test"; 
  delay(20);
}


void onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];   // payload ist NICHT null-terminiert!
  }

  Serial.print("[MOTOR_CONTROLLER] Nachricht auf ");
  Serial.print(topic);
  Serial.print(": ");
  Serial.println(message);

  if (strcmp(topic, "clawmachine/motor_controller/motor/command") == 0) {
    if (message.startsWith("x:")) {
      int speed = message.substring(2).toInt();
      movementController.move('x', speed);
    } else if (message.startsWith("y:")) {
      int speed = message.substring(2).toInt();
      movementController.move('y', speed);
    } else if (message.startsWith("z:")) {
      int speed = message.substring(2).toInt();
      movementController.move('z', speed);
    } else if (message.startsWith("claw:")) {
      String command = message.substring(5);
      movementController.moveClaw(command.c_str());
    }
  }
}
