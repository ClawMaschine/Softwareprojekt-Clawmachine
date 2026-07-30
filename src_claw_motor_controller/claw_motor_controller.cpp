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

  connection.subscribe(COMMAND_TOPIC);
}

void ClawMotorController::update()
{
  xMotorLeft.update();
  yMotor.update();
  xMotorRight.update();
  zMotor.update();
}


void ClawMotorController::move(char axis, int speed)
{
  switch (axis) {
    case 'X':
      currentX = speed;
      xMotorLeft.setSpeed(speed);
      xMotorRight.setSpeed(speed);
      Serial.printf("[MOTOR] X: %d\n", speed);
      break;
    case 'Y':
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
