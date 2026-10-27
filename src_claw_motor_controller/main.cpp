#include <Arduino.h>
#include <Wire.h>

#include "claw_mqtt_connection.h"
#include "claw_motor_controller.h"
#include "claw_ota_updater.h"
#include "firmware_config.h"
#include <ArduinoJson.h>

static constexpr const char *MOTOR_COMMAND_TOPIC  = "clawmachine/motor_controller/motor/command";
static constexpr const char *MOTOR_SETTINGS_TOPIC = "clawmachine/motor_controller/settings";
static constexpr const char *ERROR_LOG_TOPIC      = "clawmachine/motor_controller/error_log";
// PubSubClient hat standardmaessig nur 256 Byte Puffer pro Nachricht — der
// zurueckgespiegelte Original-Payload wird deshalb gekuerzt.
static constexpr size_t ERROR_LOG_MAX_PAYLOAD_LENGTH = 100;

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

ClawOtaUpdater otaUpdater(
    motorControllerConnection,
    CLAW_MOTOR_CONTROLLER_CLIENT_ID,
    CLAW_MQTT_BROKER_HOST);

// Hardware-Pruefung fuers OTA-Rollback: beide Motor-Shields muessen am I2C-Bus
// antworten, sonst wird die neue Firmware verworfen.
bool areMotorShieldsReachable()
{
  const uint8_t shieldAddresses[] = {CLAW_MOTOR_SHIELD_A_I2C_ADDRESS, CLAW_MOTOR_SHIELD_B_I2C_ADDRESS};
  for (uint8_t address : shieldAddresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
      return false;
    }
  }
  return true;
}

// Waehrend des Flashens duerfen keine Motoren weiterlaufen.
void stopAllMotorsBeforeUpdate()
{
  movementController.move('X', 0);
  movementController.move('Y', 0);
  movementController.move('Z', 0);
  movementController.update();
  otaUpdater.maintain();
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("[MOTOR_CONTROLLER] Configuration:");
  Serial.printf("[MOTOR_CONTROLLER] WiFi SSID: %s\n", CLAW_CLIENT_WIFI_SSID);
  Serial.printf("[MOTOR_CONTROLLER] MQTT Broker: %s:%d\n", CLAW_MQTT_BROKER_HOST, CLAW_MQTT_BROKER_PORT);
  Serial.printf("[MOTOR_CONTROLLER] MQTT Client ID: %s\n", CLAW_MOTOR_CONTROLLER_CLIENT_ID);
  Serial.printf("[MOTOR_CONTROLLER] MQTT User: %s\n", CLAW_MQTT_USER_USERNAME);
  Serial.printf("[MOTOR_CONTROLLER] Motor Shield A I2C Address: 0x%02X\n", CLAW_MOTOR_SHIELD_A_I2C_ADDRESS);
  Serial.printf("[MOTOR_CONTROLLER] Motor Shield B I2C Address    : 0x%02X\n", CLAW_MOTOR_SHIELD_B_I2C_ADDRESS);
  Serial.printf("[MOTOR_CONTROLLER] Max RPM: %d\n", CLAW_MOTOR_MAX_REVOLUTIONS_PER_MINUTE);
  Serial.printf("[MOTOR_CONTROLLER] Acceleration: %.1f %%/s\n", CLAW_MOTOR_ACCELERATION_PERCENT_PER_SECOND);
  Serial.printf("[MOTOR_CONTROLLER] Claw Servo Pin: %d\n", CLAW_CLAW_SERVO_PIN);
  Serial.printf("[MOTOR_CONTROLLER] OTA Allowed Host: %s\n", CLAW_MQTT_BROKER_HOST);
  Serial.println("[MOTOR_CONTROLLER] MQTT client starts");
  Serial.print("[MOTOR_CONTROLLER] Client ID: ");
  Serial.println(CLAW_MOTOR_CONTROLLER_CLIENT_ID);
  Wire.begin();
  Wire.setClock(400000); // I2C Fast Mode — PCA9685 auf dem Shield unterstuetzt das, reduziert die Zeit pro onestep()
  scanI2cBus();
  movementController.begin();
  motorControllerConnection.setMessageCallback(onMqttMessage);
  motorControllerConnection.begin();
  motorControllerConnection.subscribe(MOTOR_COMMAND_TOPIC);
  motorControllerConnection.subscribe(MOTOR_SETTINGS_TOPIC);
  otaUpdater.setBeforeUpdateCallback(stopAllMotorsBeforeUpdate);
  otaUpdater.setHealthCheck(areMotorShieldsReachable);
  otaUpdater.begin();
}

void loop()
{
  motorControllerConnection.maintainConnection();
  movementController.update();
}



// Meldet einen nicht verarbeitbaren Befehl als JSON an den Server, z.B.
// {"device":"claw_motor_controller","topic":"clawmachine/motor_controller/motor/command",
//  "payload":"X:abc","error":"Unbekannter Motorbefehl"}
void publishErrorLog(const char *sourceTopic, const String &payload, const String &errorDescription)
{
  JsonDocument jsonDoc;
  jsonDoc["device"]  = CLAW_MOTOR_CONTROLLER_CLIENT_ID;
  jsonDoc["topic"]   = sourceTopic;
  jsonDoc["payload"] = payload.substring(0, ERROR_LOG_MAX_PAYLOAD_LENGTH);
  jsonDoc["error"]   = errorDescription;

  String errorJson;
  serializeJson(jsonDoc, errorJson);
  motorControllerConnection.publish(ERROR_LOG_TOPIC, errorJson.c_str());
  Serial.printf("[MOTOR_CONTROLLER] error_log: %s\n", errorJson.c_str());
}

// Motorbefehl als JSON, z.B. {"X":100,"Y":0,"Z":0,"claw":"open"} — nur die
// enthaltenen Felder werden angewendet, fehlende Achsen bleiben unveraendert.
// Zusaetzlich wird das bisherige Textformat akzeptiert ("X:100", "claw:open"),
// das der Python-Server aktuell sendet, dazu "stop:X" fuer den Sofortstopp einer Achse.
void applyMotorCommand(const char *topic, const String &message)
{
  if (message.startsWith("{")) {
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, message);
    if (error) {
      publishErrorLog(topic, message, String("JSON ungueltig: ") + error.c_str());
      return;
    }

    const char axisNames[] = {'X', 'Y', 'Z'};
    for (char axis : axisNames) {
      const char axisKey[2] = {axis, '\0'};
      if (!jsonDoc[axisKey].isNull()) {
        if (!jsonDoc[axisKey].is<int>()) {
          publishErrorLog(topic, message, String("Wert fuer Achse ") + axis + " ist keine Zahl");
          continue;
        }
        movementController.move(axis, jsonDoc[axisKey].as<int>());
      }
    }
    if (!jsonDoc["claw"].isNull()) {
      JsonVariantConst clawValue = jsonDoc["claw"];
      // "open"/"close" oder ein Winkel in Grad (wird auf den Fahrbereich der Klaue begrenzt)
      String clawCommand = clawValue.is<const char *>() ? String(clawValue.as<const char *>())
                         : clawValue.is<int>()          ? String(clawValue.as<int>())
                                                        : String("");
      if (!movementController.moveClaw(clawCommand.c_str())) {
        publishErrorLog(topic, message, "Unbekannter Klauen-Befehl");
      }
    }
    return;
  }

  if (message.startsWith("X:") || message.startsWith("Y:") || message.startsWith("Z:")) {
    String speedText = message.substring(2);
    char *speedEnd = nullptr;
    long speed = strtol(speedText.c_str(), &speedEnd, 10);
    if (speedText.length() == 0 || *speedEnd != '\0') {
      publishErrorLog(topic, message, "Geschwindigkeit ist keine Zahl");
      return;
    }
    movementController.move(message.charAt(0), (int)speed);
  } else if (message.startsWith("stop:")) {
    // "stop:X" - Achse sofort anhalten, ohne Rampe (Endstop, Homing)
    const String axisText = message.substring(5);
    if (axisText.length() != 1 || !movementController.stopAxisImmediately(axisText.charAt(0))) {
      publishErrorLog(topic, message, "Unbekannte Achse beim Sofortstopp");
    }
  } else if (message.startsWith("claw:")) {
    if (!movementController.moveClaw(message.substring(5).c_str())) {
      publishErrorLog(topic, message, "Unbekannter Klauen-Befehl");
    }
  } else {
    publishErrorLog(topic, message, "Unbekannter Motorbefehl");
  }
}

void onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];   // payload ist NICHT null-terminiert!
  }

  // Fahrbefehle kommen bis zu mehrmals pro Sekunde. Jede serielle Ausgabe blockiert bei
  // 115200 Baud einige Millisekunden den loop() und damit die Schritte der Motoren
  // (spuerbar als Ruck). Deshalb hier nicht mitprotokollieren; die Achsen melden ihre
  // Geschwindigkeit selbst in ClawMotorController::move().
  if (strcmp(topic, MOTOR_COMMAND_TOPIC) != 0) {
    Serial.print("[MOTOR_CONTROLLER] Nachricht auf ");
    Serial.print(topic);
    Serial.print(": ");
    Serial.println(message);
  }

  if (otaUpdater.handleMessage(topic, message)) {
    return;
  }

  if (strcmp(topic, MOTOR_SETTINGS_TOPIC) == 0) {
    // JSON-Settings-Update, z.B. {"accelerationPercentPerSecond": 50} (alle Achsen), {"xyAccelerationPercentPerSecond": 50},
    // {"zAccelerationPercentPerSecond": 50} oder {"clawSpeedDegreesPerSecond": 35}
    JsonDocument jsonDoc;
    DeserializationError error = deserializeJson(jsonDoc, message);
    if (error) {
      publishErrorLog(topic, message, String("JSON ungueltig: ") + error.c_str());
      return;
    }

    bool hasKnownSetting = false;
    if (jsonDoc["accelerationPercentPerSecond"].is<float>()) {
      movementController.setAcceleration(jsonDoc["accelerationPercentPerSecond"].as<float>());
      hasKnownSetting = true;
    }
    if (jsonDoc["xyAccelerationPercentPerSecond"].is<float>()) {
      movementController.setXyAcceleration(jsonDoc["xyAccelerationPercentPerSecond"].as<float>());
      hasKnownSetting = true;
    }
    if (jsonDoc["zAccelerationPercentPerSecond"].is<float>()) {
      movementController.setZAcceleration(jsonDoc["zAccelerationPercentPerSecond"].as<float>());
      hasKnownSetting = true;
    }
    if (jsonDoc["clawSpeedDegreesPerSecond"].is<float>()) {
      movementController.setClawSpeed(jsonDoc["clawSpeedDegreesPerSecond"].as<float>());
      hasKnownSetting = true;
    }
    if (!hasKnownSetting) {
      publishErrorLog(topic, message,
                      "Keine bekannte Einstellung (accelerationPercentPerSecond, xy/zAccelerationPercentPerSecond, clawSpeedDegreesPerSecond) oder keine Zahl");
    }
  } else if (strcmp(topic, MOTOR_COMMAND_TOPIC) == 0) {
    applyMotorCommand(topic, message);
  }
}
