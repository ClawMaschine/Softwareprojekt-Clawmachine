
#include "claw_servo_motor.h"
#include <math.h>

ClawServoMotor::ClawServoMotor(
    uint8_t servoPin,
    uint8_t openAngleDegrees,
    uint8_t closedAngleDegrees)
    : servoPin(servoPin),
      openAngleDegrees(openAngleDegrees),
      closedAngleDegrees(closedAngleDegrees)
{
  currentAngle = openAngleDegrees;
  targetAngle = openAngleDegrees;
}

void ClawServoMotor::begin()
{
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo.setPeriodHertz(SERVO_PERIOD_HERTZ);

  const int attachedChannel = servo.attach(
      servoPin,
      SERVO_MIN_PULSE_MICROSECONDS,
      SERVO_MAX_PULSE_MICROSECONDS);

  if (!servo.attached()) {
    Serial.printf(
        "[SERVO] FEHLER: attach auf GPIO %u fehlgeschlagen\n",
        servoPin);
  } else {
    Serial.printf(
        "[SERVO] GPIO %u, LEDC-Kanal %d, %d-%d us, %d Hz\n",
        servoPin,
        attachedChannel,
        SERVO_MIN_PULSE_MICROSECONDS,
        SERVO_MAX_PULSE_MICROSECONDS,
        SERVO_PERIOD_HERTZ);

    servo.write(static_cast<int>(roundf(currentAngle)));
    lastUpdateMicroseconds = micros();
  }
}

void ClawServoMotor::setSpeed(float degreesPerSecond)
{
  speedDegreesPerSecond =
    (degreesPerSecond < 1.0f) ? 1.0f : degreesPerSecond;
}

void ClawServoMotor::setAngle(uint8_t angleDegrees)
{
  angleDegrees = constrain(
      angleDegrees,
      MIN_ALLOWED_ANGLE_DEGREES,
      MAX_ALLOWED_ANGLE_DEGREES);

  if (!servo.attached()) {
    Serial.println(
        "[SERVO] FEHLER: Servo nicht angehaengt, Befehl ignoriert");
    return;
  }

  targetAngle = angleDegrees;

  Serial.printf("[SERVO] Zielwinkel: %u Grad\n", angleDegrees);
}

void ClawServoMotor::open()
{
  setAngle(openAngleDegrees);
}

void ClawServoMotor::close()
{
  setAngle(closedAngleDegrees);
}


void ClawServoMotor::update()
{
  if (!servo.attached()) {
    return;
  }

  const unsigned long now = micros();

  const float elapsedSeconds =
      (now - lastUpdateMicroseconds) / 1000000.0f;

  lastUpdateMicroseconds = now;

  // Zielwinkel auf den erlaubten Bereich begrenzen
  targetAngle = constrain(
      targetAngle,
      MIN_ALLOWED_ANGLE_DEGREES,
      MAX_ALLOWED_ANGLE_DEGREES);

  if (currentAngle == targetAngle) {
    return;
  }

  const float difference = targetAngle - currentAngle;
  const float maxStep = speedDegreesPerSecond * elapsedSeconds;

  if (fabsf(difference) <= maxStep) {
    currentAngle = targetAngle;
  } else {
    currentAngle += (difference > 0.0f ? maxStep : -maxStep);
  }

  // Auch den tatsächlichen Ausgabewinkel begrenzen
  currentAngle = constrain(
      currentAngle,
      static_cast<float>(MIN_ALLOWED_ANGLE_DEGREES),
      static_cast<float>(MAX_ALLOWED_ANGLE_DEGREES));

  servo.write(static_cast<int>(roundf(currentAngle)));
}

