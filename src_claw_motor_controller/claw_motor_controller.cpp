#include "claw_motor_controller.h"

ClawMotorController *ClawMotorController::instance = nullptr;

ClawMotorController::ClawMotorController(
    ClawMqttConnection &connection,
    uint8_t motorShieldAI2cAddress,
    uint8_t motorShieldBI2cAddress,
    uint16_t maxRevolutionsPerMinute,
    uint8_t clawServoPin)
    : connection(connection),
      motorShieldA(motorShieldAI2cAddress),
      motorShieldB(motorShieldBI2cAddress),
      xMotorLeft(motorShieldA, 1, maxRevolutionsPerMinute),
      yMotor(motorShieldA, 2, maxRevolutionsPerMinute),
      xMotorRight(motorShieldB, 1, maxRevolutionsPerMinute),
      zMotor(motorShieldB, 2, maxRevolutionsPerMinute),
      clawServo(clawServoPin)
{
  instance = this;
}

void ClawMotorController::begin()
{
  motorShieldA.begin();
  motorShieldB.begin();

  xMotorLeft.begin();
  yMotor.begin();
  xMotorRight.begin();
  zMotor.begin();
  clawServo.begin();

  connection.setMessageCallback(onMqttMessage);
  connection.subscribe(COMMAND_TOPIC);
}

void ClawMotorController::update()
{
  xMotorLeft.update();
  yMotor.update();
  xMotorRight.update();
  zMotor.update();
}

void ClawMotorController::onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  if (instance == nullptr) {
    return;
  }

  char payloadStr[length + 1];
  memcpy(payloadStr, payload, length);
  payloadStr[length] = '\0';

  if (strncmp(payloadStr, "x:", 2) == 0) {
    instance->move('x', atoi(payloadStr + 2));
  } else if (strncmp(payloadStr, "y:", 2) == 0) {
    instance->move('y', atoi(payloadStr + 2));
  } else if (strncmp(payloadStr, "z:", 2) == 0) {
    instance->moveZ(constrain(atoi(payloadStr + 2), -100, 100));
  } else if (strncmp(payloadStr, "claw:", 5) == 0) {
    instance->moveClaw(payloadStr + 5);
  } else {
    Serial.print("[MOTOR] Unknown command: ");
    Serial.println(payloadStr);
  }
}

void ClawMotorController::move(char axis, int speed)
{
  switch (axis) {
    case 'x':
      currentX = speed;
      // TODO: Drehrichtung eines Motors invertieren, falls beide X-Motoren gegenläufig montiert sind.
      xMotorLeft.setSpeed(speed);
      xMotorRight.setSpeed(speed);
      Serial.printf("[MOTOR] X: %d\n", speed);
      break;
    case 'y':
      currentY = speed;
      yMotor.setSpeed(speed);
      Serial.printf("[MOTOR] Y: %d\n", speed);
      break;
  }
}

void ClawMotorController::moveZ(int speed)
{
  ropeSpeed = speed;
  zMotor.setSpeed(speed);
  Serial.printf("[MOTOR] Z (Seil): %d\n", speed);
}

void ClawMotorController::moveClaw(const char *command)
{
  if (strcmp(command, "open") == 0) {
    clawServo.open();
    Serial.println("[MOTOR] Klaue: open");
  } else if (strcmp(command, "close") == 0) {
    clawServo.close();
    Serial.println("[MOTOR] Klaue: close");
  } else {
    Serial.printf("[MOTOR] Klaue: unbekannter Befehl: %s\n", command);
  }
}
