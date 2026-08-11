#include <Arduino.h>
#include <Wire.h>

#include "claw_mqtt_connection.h"
#include "claw_motor_controller.h"
#include "firmware_config.h"
#include <ArduinoJson.h>

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);

void scanI2cBus()
{
  Serial.println("[I2C] Scanne Bus nach angeschlossenen Geraeten...");
  uint8_t foundDeviceCount = 0;
  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    uint8_t transmissionError = Wire.endTransmission();
    if (transmissionError == 0) {
      Serial.printf("[I2C] Geraet gefunden bei Adresse 0x%02X\n", address);
      foundDeviceCount++;
    }
  }
  if (foundDeviceCount == 0) {
    Serial.println("[I2C] Keine Geraete gefunden! Verkabelung/Stromversorgung pruefen.");
  }
}

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
    CLAW_CLAW_SERVO_PIN,
    CLAW_MOTOR_ACCELERATION_PERCENT_PER_SECOND);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[MOTOR_CONTROLLER] MQTT client starts");
  Serial.print("[MOTOR_CONTROLLER] Client ID: ");
  Serial.println(CLAW_MOTOR_CONTROLLER_CLIENT_ID);
  Wire.begin();
  Wire.setClock(400000); // I2C Fast Mode — PCA9685 auf dem Shield unterstuetzt das, reduziert die Zeit pro onestep()
  scanI2cBus();
  motorControllerConnection.begin();
  movementController.begin();
  motorControllerConnection.setMessageCallback(onMqttMessage);
  motorControllerConnection.begin();
  motorControllerConnection.subscribe("clawmachine/motor_controller/motor/command");
  motorControllerConnection.subscribe("clawmachine/motor_controller/settings");
}

void loop()
{
  motorControllerConnection.maintainConnection();
  movementController.update();
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

  if (strcmp(topic, "clawmachine/motor_controller/settings") == 0) {
    // JSON-Settings-Update, z.B. {"accelerationPercentPerSecond": 50}
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, message);
    if (error) {
      Serial.print("[MOTOR_CONTROLLER] Fehler beim Parsen der Settings-Nachricht: ");
      Serial.println(error.c_str());
      return;
    }

    if (!jsonDoc["accelerationPercentPerSecond"].isNull()) {
      float newAcceleration = jsonDoc["accelerationPercentPerSecond"].as<float>();
      movementController.setAcceleration(newAcceleration);
    }
  } else if (strcmp(topic, "clawmachine/motor_controller/motor/command") == 0) {
    if (message.startsWith("X:")) {
      int speed = message.substring(2).toInt();
      movementController.move('X', speed);
    } else if (message.startsWith("Y:")) {
      int speed = message.substring(2).toInt();
      movementController.move('Y', speed);
    } else if (message.startsWith("Z:")) {
      int speed = message.substring(2).toInt();
      movementController.moveZ(speed);
    } else if (message.startsWith("claw:")) {
      String command = message.substring(5);
      movementController.moveClaw(command.c_str());
    }
  }
}
