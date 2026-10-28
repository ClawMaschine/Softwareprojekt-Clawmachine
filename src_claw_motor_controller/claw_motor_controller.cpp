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
      xyAccelerationPercentPerSecond(accelerationPercentPerSecond),
      zAccelerationPercentPerSecond(accelerationPercentPerSecond),
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

  xMotorLeft.setAcceleration(xyAccelerationPercentPerSecond);
  yMotor.setAcceleration(xyAccelerationPercentPerSecond);
  xMotorRight.setAcceleration(xyAccelerationPercentPerSecond);
  zMotor.setAcceleration(zAccelerationPercentPerSecond);
}

void ClawMotorController::update()
{
  xMotorLeft.update();
  yMotor.update();
  xMotorRight.update();
  zMotor.update();
  clawServo.update();
}


bool ClawMotorController::move(char axis, int speed)
{
  switch (axis) {
    case 'X':
      currentX = speed;
      // xMotorLeft und xMotorRight sind gegensinnig montiert (siehe altes Referenzprogramm) —
      // ohne Spiegelung wuerden sie gegeneinander statt gemeinsam fahren.
      xMotorLeft.setSpeed(-speed);
      xMotorRight.setSpeed(speed);
      Serial.printf("[MOTOR] X: %d\n", speed);
      break;
    case 'Y':
      speed = -speed; // Invertiere Geschwindigkeit, damit positive Werte nach rechts/vorne/hoch fahren.
      currentY = speed;
      yMotor.setSpeed(speed);
      Serial.printf("[MOTOR] Y: %d\n", speed);
      break;
    case 'Z':
      zMotor.setSpeed(-speed);
      break;
    default:
      Serial.printf("[MOTOR] Unbekannte Achse: %c\n", axis);
      return false;
  }
  return true;
}

void ClawMotorController::setClawSpeed(float degreesPerSecond)
{
  clawServo.setSpeed(degreesPerSecond);
}

void ClawMotorController::setClawJogSpeed(float degreesPerSecond)
{
  clawServo.setJogSpeed(degreesPerSecond);
}

void ClawMotorController::setAcceleration(float percentPerSecond)
{
  setXyAcceleration(percentPerSecond);
  setZAcceleration(percentPerSecond);
}

void ClawMotorController::setXyAcceleration(float percentPerSecond)
{
  xyAccelerationPercentPerSecond = percentPerSecond;

  xMotorLeft.setAcceleration(percentPerSecond);
  yMotor.setAcceleration(percentPerSecond);
  xMotorRight.setAcceleration(percentPerSecond);

  Serial.printf("[MOTOR] Beschleunigung X/Y geaendert: %.1f %%/s\n", percentPerSecond);
}

void ClawMotorController::setZAcceleration(float percentPerSecond)
{
  zAccelerationPercentPerSecond = percentPerSecond;

  zMotor.setAcceleration(percentPerSecond);

  Serial.printf("[MOTOR] Beschleunigung Z geaendert: %.1f %%/s\n", percentPerSecond);
}

void ClawMotorController::setXyMaxRevolutionsPerMinute(uint16_t revolutionsPerMinute)
{
  xMotorLeft.setMaxRevolutionsPerMinute(revolutionsPerMinute);
  yMotor.setMaxRevolutionsPerMinute(revolutionsPerMinute);
  xMotorRight.setMaxRevolutionsPerMinute(revolutionsPerMinute);

  Serial.printf("[MOTOR] Max. Drehzahl X/Y geaendert: %u RPM\n", revolutionsPerMinute);
}

void ClawMotorController::setZMaxRevolutionsPerMinute(uint16_t revolutionsPerMinute)
{
  zMotor.setMaxRevolutionsPerMinute(revolutionsPerMinute);

  Serial.printf("[MOTOR] Max. Drehzahl Z geaendert: %u RPM\n", revolutionsPerMinute);
}

// Die Motoren zaehlen in ihrer physischen Drehrichtung. Die Vorzeichen hier sind dieselben
// wie in move(): X rechts +1, Y und Z -1 (X links gegensinnig, wird ueber den rechten Motor gelesen).
long ClawMotorController::getPosition(char axis) const
{
  switch (axis) {
    case 'X': return xMotorRight.getStepPosition();
    case 'Y': return -yMotor.getStepPosition();
    case 'Z': return -zMotor.getStepPosition();
    default:  return 0;
  }
}

bool ClawMotorController::zeroPosition(char axis)
{
  switch (axis) {
    case 'X':
      xMotorLeft.resetStepPosition(0);
      xMotorRight.resetStepPosition(0);
      break;
    case 'Y':
      yMotor.resetStepPosition(0);
      break;
    case 'Z':
      zMotor.resetStepPosition(0);
      break;
    default:
      Serial.printf("[MOTOR] Unbekannte Achse: %c\n", axis);
      return false;
  }
  Serial.printf("[MOTOR] %c: Position auf 0 gesetzt\n", axis);
  return true;
}

bool ClawMotorController::moveToPosition(char axis, long steps, int speedPercent)
{
  switch (axis) {
    case 'X':
      xMotorRight.moveToStepPosition(steps, speedPercent);
      xMotorLeft.moveToStepPosition(-steps, speedPercent);
      break;
    case 'Y':
      yMotor.moveToStepPosition(-steps, speedPercent);
      break;
    case 'Z':
      zMotor.moveToStepPosition(-steps, speedPercent);
      break;
    default:
      Serial.printf("[MOTOR] Unbekannte Achse: %c\n", axis);
      return false;
  }
  Serial.printf("[MOTOR] %c: Fahre zu Position %ld mit %d %%\n", axis, steps, speedPercent);
  return true;
}

bool ClawMotorController::stopAxisImmediately(char axis)
{
  switch (axis) {
    case 'X':
      currentX = 0;
      xMotorLeft.stopImmediately();
      xMotorRight.stopImmediately();
      break;
    case 'Y':
      currentY = 0;
      yMotor.stopImmediately();
      break;
    case 'Z':
      zMotor.stopImmediately();
      break;
    default:
      Serial.printf("[MOTOR] Unbekannte Achse: %c\n", axis);
      return false;
  }
  Serial.printf("[MOTOR] %c: Sofortstopp\n", axis);
  return true;
}

bool ClawMotorController::moveClaw(const char *command)
{
  if (strcmp(command, "open") == 0) {
    clawServo.open();
    Serial.println("[MOTOR] Klaue: open");
  } else if (strcmp(command, "close") == 0) {
    clawServo.close();
    Serial.println("[MOTOR] Klaue: close");
  } else if (strcmp(command, "jog_open") == 0) {
    clawServo.jog(-1);
  } else if (strcmp(command, "jog_close") == 0) {
    clawServo.jog(1);
  } else if (strcmp(command, "stop") == 0) {
    clawServo.stopMoving();
  } else {
    // Winkel in Grad als Zahl, z.B. "120"; ClawServoMotor begrenzt auf den erlaubten Bereich
    char *angleEnd = nullptr;
    const long angleDegrees = strtol(command, &angleEnd, 10);
    if (command[0] == '\0' || *angleEnd != '\0') {
      Serial.printf("[MOTOR] Klaue: unbekannter Befehl: %s\n", command);
      return false;
    }
    clawServo.setAngle(static_cast<uint8_t>(constrain(angleDegrees, 0L, 255L)));
  }
  return true;
}
