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
  hasTargetStepPosition = false;
  targetSpeedPercent = constrain(speedPercent, -100, 100);

  if (accelerationPercentPerSecond <= 0) {
    // Keine Rampe konfiguriert — wie bisher: Geschwindigkeit springt sofort.
    applySpeed(targetSpeedPercent);
  }
}

void ClawStepperMotor::stopImmediately()
{
  hasTargetStepPosition = false;
  targetSpeedPercent = 0;
  applySpeed(0);
}

long ClawStepperMotor::getStepPosition() const
{
  return stepPosition;
}

int ClawStepperMotor::getTargetSpeedPercent() const
{
  return targetSpeedPercent;
}

void ClawStepperMotor::resetStepPosition(long newStepPosition)
{
  stepPosition = newStepPosition;
  // Eine laufende Zielfahrt bezog sich auf das alte Koordinatensystem.
  if (hasTargetStepPosition) {
    stopImmediately();
  }
}

void ClawStepperMotor::moveToStepPosition(long targetStepPositionValue, int speedPercent)
{
  if (hasStepPositionLimits) {
    // Sonst bliebe die Zielfahrt mit gesetztem hasTargetStepPosition stehen, sobald der
    // Schritt-Check an der Grenze bremst, und das Ziel wuerde nie als erreicht gemeldet.
    targetStepPositionValue = constrain(targetStepPositionValue, minStepPosition, maxStepPosition);
  }

  const long stepsToGo = targetStepPositionValue - stepPosition;
  if (stepsToGo == 0) {
    stopImmediately();
    return;
  }

  const int speedMagnitudePercent = abs(speedPercent);
  setSpeed(stepsToGo > 0 ? speedMagnitudePercent : -speedMagnitudePercent);
  // Nach setSpeed(), das die Zielfahrt sonst gleich wieder verwirft.
  hasTargetStepPosition = true;
  targetStepPosition    = targetStepPositionValue;
}

void ClawStepperMotor::setStepPositionLimits(long newMinStepPosition, long newMaxStepPosition)
{
  hasStepPositionLimits = true;
  minStepPosition       = min(newMinStepPosition, newMaxStepPosition);
  maxStepPosition       = max(newMinStepPosition, newMaxStepPosition);
}

void ClawStepperMotor::clearStepPositionLimits()
{
  hasStepPositionLimits = false;
}

bool ClawStepperMotor::consumeStepPositionLimitHit()
{
  const bool hasHit         = hasHitStepPositionLimit;
  hasHitStepPositionLimit   = false;
  return hasHit;
}

bool ClawStepperMotor::isBlockedByStepPositionLimit() const
{
  // Entscheidend ist die tatsaechliche Geschwindigkeit und nicht targetSpeedPercent: beim
  // Loslassen der Taste sendet der Server 0, der Motor laeuft aber noch die Bremsrampe
  // herunter und faehrt dabei weiter.
  if (!hasStepPositionLimits || currentSpeedPercent == 0) {
    return false;
  }
  const long nextStepPosition = stepPosition + (currentSpeedPercent > 0 ? 1 : -1);
  return nextStepPosition > maxStepPosition || nextStepPosition < minStepPosition;
}

void ClawStepperMotor::setAcceleration(float percentPerSecond)
{
  accelerationPercentPerSecond = max(percentPerSecond, 0.0f);
}

void ClawStepperMotor::setMaxRevolutionsPerMinute(uint16_t newMaxRevolutionsPerMinute)
{
  maxRevolutionsPerMinute = newMaxRevolutionsPerMinute;
  applySpeed(currentSpeedPercent);
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

  // Vor jedem einzelnen Schritt pruefen, nicht nur bei jedem Fahrbefehl: so kann weder eine
  // auslaufende Bremsrampe noch ein wiederholter Fahrbefehl das Fenster verlassen. Der Stopp
  // setzt die Geschwindigkeit auf 0, der Motor laeuft damit gleich in den release()-Zweig.
  if (isBlockedByStepPositionLimit()) {
    stopImmediately();
    hasHitStepPositionLimit = true;
  }

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
  const bool isMovingForward = currentSpeedPercent > 0;
  stepperMotor->onestep(isMovingForward ? FORWARD : BACKWARD, DOUBLE);
  stepPosition += isMovingForward ? 1 : -1;

  if (hasTargetStepPosition &&
      (isMovingForward ? stepPosition >= targetStepPosition : stepPosition <= targetStepPosition)) {
    stopImmediately();
  }
}
