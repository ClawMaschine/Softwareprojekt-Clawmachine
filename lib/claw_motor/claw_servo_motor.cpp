#include "claw_servo_motor.h"

ClawServoMotor::ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees, uint8_t closedAngleDegrees)
    : servoPin(servoPin), openAngleDegrees(openAngleDegrees), closedAngleDegrees(closedAngleDegrees)
{
}

void ClawServoMotor::begin()
{
  servo.attach(servoPin);
}

void ClawServoMotor::open()
{
  servo.write(openAngleDegrees);
}

void ClawServoMotor::close()
{
  servo.write(closedAngleDegrees);
}
