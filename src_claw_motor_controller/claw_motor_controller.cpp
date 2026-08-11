#include "claw_motor_controller.h"

ClawMotorController *ClawMotorController::instance = nullptr;

ClawMotorController::ClawMotorController(
    ClawMqttConnection &connection,
    uint8_t motorShieldAI2cAddress,
    uint8_t motorShieldBI2cAddress,
    uint16_t maxRevolutionsPerMinute,
    uint8_t clawServoPin,
    float accelerationPercentPerSecond)
    : connection(connection),
      motorShieldAI2cAddress(motorShieldAI2cAddress),
      motorShieldBI2cAddress(motorShieldBI2cAddress),
      accelerationPercentPerSecond(accelerationPercentPerSecond),
      motorShieldA(motorShieldAI2cAddress),
      motorShieldB(motorShieldBI2cAddress),
      xMotorLeft(motorShieldB, 1, maxRevolutionsPerMinute),
      yMotor(motorShieldA, 1, maxRevolutionsPerMinute),
      xMotorRight(motorShieldB, 2, maxRevolutionsPerMinute),
      zMotor(motorShieldA, 2, maxRevolutionsPerMinute),
      clawServo(clawServoPin)
{
  instance = this;
}

void ClawMotorController::begin()
{
  if (!motorShieldA.begin()) {
    Serial.printf("[MOTOR] FEHLER: Motor Shield A (I2C 0x%02X) antwortet nicht!\n", motorShieldAI2cAddress);
  }
  if (!motorShieldB.begin()) {
    Serial.printf("[MOTOR] FEHLER: Motor Shield B (I2C 0x%02X) antwortet nicht!\n", motorShieldBI2cAddress);
  }

  xMotorLeft.begin();
  yMotor.begin();
  xMotorRight.begin();
  zMotor.begin();
  clawServo.begin();

  xMotorLeft.setAcceleration(accelerationPercentPerSecond);
  yMotor.setAcceleration(accelerationPercentPerSecond);
  xMotorRight.setAcceleration(accelerationPercentPerSecond);
  zMotor.setAcceleration(accelerationPercentPerSecond);

  // Permanenter Log, damit sich "wird die Beschleunigung wirklich genutzt?"
  // direkt am Serial-Monitor beim Booten beantworten laesst, ohne den Code
  // lesen zu muessen. 0 heisst: kein Ramping, Geschwindigkeit springt sofort.
  Serial.printf("[MOTOR] Beschleunigung: %.1f %%/s\n", accelerationPercentPerSecond);

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
      // xMotorLeft und xMotorRight sind gegensinnig montiert (siehe altes Referenzprogramm) —
      // ohne Spiegelung wuerden sie gegeneinander statt gemeinsam fahren.
      xMotorLeft.setSpeed(speed);
      xMotorRight.setSpeed(-speed);
      Serial.printf("[MOTOR] X: %d\n", speed);
      break;
    case 'Y':
      speed = -speed; // Invertiere Geschwindigkeit, damit positive Werte nach rechts/vorne/hoch fahren.
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

void ClawMotorController::setAcceleration(float percentPerSecond)
{
  accelerationPercentPerSecond = percentPerSecond;

  xMotorLeft.setAcceleration(percentPerSecond);
  yMotor.setAcceleration(percentPerSecond);
  xMotorRight.setAcceleration(percentPerSecond);
  zMotor.setAcceleration(percentPerSecond);

  Serial.printf("[MOTOR] Beschleunigung geaendert: %.1f %%/s\n", percentPerSecond);
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
