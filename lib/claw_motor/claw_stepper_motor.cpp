#include "claw_stepper_motor.h"

ClawStepperMotor::ClawStepperMotor(Adafruit_MotorShield &motorShield, uint8_t stepperPort, uint16_t maxRevolutionsPerMinute)
    : motorShield(motorShield),
      stepperPort(stepperPort),
      maxRevolutionsPerMinute(maxRevolutionsPerMinute),
      stepperMotor(nullptr)
{
}

void ClawStepperMotor::begin()
{
  stepperMotor         = motorShield.getStepper(STEPS_PER_REVOLUTION, stepperPort);
  lastRampMicroseconds = micros();
}

void ClawStepperMotor::setSpeed(int speedPercent)
{
  targetSpeedPercent = constrain(speedPercent, -100, 100);

  if (accelerationPercentPerSecond <= 0) {
    // Keine Rampe konfiguriert — wie bisher: Geschwindigkeit springt sofort.
    applySpeed(targetSpeedPercent);
  }
}

void ClawStepperMotor::setAcceleration(float percentPerSecond)
{
  accelerationPercentPerSecond = max(percentPerSecond, 0.0f);
}

void ClawStepperMotor::applySpeed(float speedPercent)
{
  currentSpeedPercent = constrain(speedPercent, -100.0f, 100.0f);

  if (currentSpeedPercent == 0) {
    stepIntervalMicroseconds = 0;
    return;
  }

  float revolutionsPerMinute = maxRevolutionsPerMinute * fabs(currentSpeedPercent) / 100.0f;
  float stepsPerSecond       = revolutionsPerMinute / 60.0f * STEPS_PER_REVOLUTION;
  stepIntervalMicroseconds   = (unsigned long)(1000000.0f / stepsPerSecond);
}

void ClawStepperMotor::updateRamp()
{
  unsigned long nowMicroseconds = micros();

  if (accelerationPercentPerSecond <= 0 || currentSpeedPercent == targetSpeedPercent) {
    lastRampMicroseconds = nowMicroseconds;
    return;
  }

  float elapsedSeconds = (nowMicroseconds - lastRampMicroseconds) / 1000000.0f;
  lastRampMicroseconds = nowMicroseconds;

  float maxStep = accelerationPercentPerSecond * elapsedSeconds;
  float diff    = targetSpeedPercent - currentSpeedPercent;

  // Kein Runden auf ganze Prozentpunkte mehr — update() kann sehr oft pro
  // Sekunde aufgerufen werden, da darf ein einzelner Aufruf nicht mindestens
  // 1 Prozentpunkt Fortschritt erzwingen (siehe Kommentar im Header).
  if (fabs(diff) <= maxStep) {
    applySpeed(targetSpeedPercent);
  } else {
    applySpeed(currentSpeedPercent + (diff > 0 ? maxStep : -maxStep));
  }
}

void ClawStepperMotor::update()
{
  if (stepperMotor == nullptr) {
    return;
  }

  updateRamp();

  if (stepIntervalMicroseconds == 0) {
    // Motor steht still (Rampe ist bei 0% angekommen) -> Spulen stromlos
    // schalten, damit der Motor im Leerlauf nicht unnötig Strom zieht und
    // sich nicht erwärmt. Nur einmal beim Übergang in den Stillstand
    // aufrufen, nicht bei jedem update()-Durchlauf.
    if (!isStepperReleased) {
      stepperMotor->release();
      isStepperReleased = true;
    }
    return;
  }

  isStepperReleased = false;

  unsigned long nowMicroseconds = micros();
  if (nowMicroseconds - lastStepMicroseconds < stepIntervalMicroseconds) {
    return;
  }

  lastStepMicroseconds = nowMicroseconds;
  stepperMotor->onestep(currentSpeedPercent > 0 ? FORWARD : BACKWARD, DOUBLE);
}
