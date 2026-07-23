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
  stepperMotor = motorShield.getStepper(STEPS_PER_REVOLUTION, stepperPort);
}

void ClawStepperMotor::setSpeed(int speedPercent)
{
  currentSpeedPercent = constrain(speedPercent, -100, 100);

  if (currentSpeedPercent == 0) {
    stepIntervalMicroseconds = 0;
    return;
  }

  float revolutionsPerMinute = maxRevolutionsPerMinute * abs(currentSpeedPercent) / 100.0f;
  float stepsPerSecond       = revolutionsPerMinute / 60.0f * STEPS_PER_REVOLUTION;
  stepIntervalMicroseconds   = (unsigned long)(1000000.0f / stepsPerSecond);
}

void ClawStepperMotor::update()
{
  if (stepperMotor == nullptr || stepIntervalMicroseconds == 0) {
    return;
  }

  unsigned long nowMicroseconds = micros();
  if (nowMicroseconds - lastStepMicroseconds < stepIntervalMicroseconds) {
    return;
  }

  lastStepMicroseconds = nowMicroseconds;
  stepperMotor->onestep(currentSpeedPercent > 0 ? FORWARD : BACKWARD, DOUBLE);
}
