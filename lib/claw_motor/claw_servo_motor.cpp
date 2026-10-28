#include "claw_servo_motor.h"

#include <math.h>

ClawServoMotor::ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees, uint8_t closedAngleDegrees)
    : currentAngleDegrees(openAngleDegrees),
      targetAngleDegrees(openAngleDegrees),
      servoPin(servoPin),
      openAngleDegrees(openAngleDegrees),
      closedAngleDegrees(closedAngleDegrees)
{
}

void ClawServoMotor::begin()
{
  // ESP32Servo bekommt seine LEDC-Timer nur, wenn sie vorher freigegeben
  // wurden; ohne diese Aufrufe bleibt das PWM-Signal je nach Library-/Core-
  // Version stumm, obwohl attach() "klappt".
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servo.setPeriodHertz(SERVO_PERIOD_HERTZ);
  const int attachedChannel = servo.attach(servoPin, SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS);

  // Kanal 0 ist ein gueltiger LEDC-Kanal (der erste Servo bekommt ihn), deshalb
  // nur attached() auswerten. Die ESP32Servo-Logzeilen "Pin ... is already attached
  // to LEDC" / "PWM channel failed to configure" stammen aus einem doppelten
  // internen Attach der Library und sind harmlos, wenn attached() true ist.
  if (!servo.attached()) {
    Serial.printf("[SERVO] FEHLER: attach auf GPIO %u fehlgeschlagen (Pin nicht PWM-faehig oder keine Timer frei)\n", servoPin);
  } else {
    Serial.printf("[SERVO] GPIO %u, LEDC-Kanal %d, %d-%d us, %d Hz\n", servoPin, attachedChannel,
                  SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS, SERVO_PERIOD_HERTZ);
    // Startposition = "offen", damit currentAngleDegrees zur Hardware passt
    writeAngle(currentAngleDegrees);
    lastUpdateMicroseconds = micros();
  }
}

// Pulsbreite direkt aus dem Winkel (0-180 Grad = Min-Max-Pulsbreite) in
// Mikrosekunden statt ganzer Grad, damit langsame Fahrten nicht in Stufen laufen.
void ClawServoMotor::writeAngle(float angleDegrees)
{
  const float pulseMicroseconds =
      SERVO_MIN_PULSE_MICROSECONDS +
      angleDegrees / 180.0f * (SERVO_MAX_PULSE_MICROSECONDS - SERVO_MIN_PULSE_MICROSECONDS);
  servo.writeMicroseconds(static_cast<int>(roundf(pulseMicroseconds)));
}

void ClawServoMotor::setSpeed(float degreesPerSecond)
{
  speedDegreesPerSecond = max(degreesPerSecond, MIN_SPEED_DEGREES_PER_SECOND);
  Serial.printf("[SERVO] Geschwindigkeit: %.1f Grad/s\n", speedDegreesPerSecond);
}

void ClawServoMotor::update()
{
  if (!servo.attached()) {
    return;
  }
  const unsigned long nowMicroseconds = micros();
  const float elapsedSeconds = (nowMicroseconds - lastUpdateMicroseconds) / 1000000.0f;
  lastUpdateMicroseconds = nowMicroseconds;

  if (currentAngleDegrees == targetAngleDegrees) {
    return;
  }
  const float remainingDegrees = targetAngleDegrees - currentAngleDegrees;
  const float activeSpeedDegreesPerSecond = isJogging ? jogSpeedDegreesPerSecond : speedDegreesPerSecond;
  const float maxStepDegrees = activeSpeedDegreesPerSecond * elapsedSeconds;
  if (fabsf(remainingDegrees) <= maxStepDegrees) {
    currentAngleDegrees = targetAngleDegrees;
  } else {
    currentAngleDegrees += (remainingDegrees > 0.0f ? maxStepDegrees : -maxStepDegrees);
  }
  writeAngle(currentAngleDegrees);
}

void ClawServoMotor::setAngle(uint8_t angleDegrees)
{
  angleDegrees = constrain(angleDegrees, MIN_ALLOWED_ANGLE_DEGREES, MAX_ALLOWED_ANGLE_DEGREES);
  if (!servo.attached()) {
    Serial.println("[SERVO] FEHLER: Servo nicht angehaengt, Befehl ignoriert");
    return;
  }
  isJogging = false;
  targetAngleDegrees = angleDegrees;
  Serial.printf("[SERVO] Zielwinkel: %u Grad\n", angleDegrees);
}

void ClawServoMotor::setJogSpeed(float degreesPerSecond)
{
  jogSpeedDegreesPerSecond = max(degreesPerSecond, MIN_SPEED_DEGREES_PER_SECOND);
  Serial.printf("[SERVO] Jog-Geschwindigkeit: %.1f Grad/s\n", jogSpeedDegreesPerSecond);
}

void ClawServoMotor::jog(int direction)
{
  if (!servo.attached()) {
    return;
  }
  isJogging = true;
  targetAngleDegrees = direction < 0 ? openAngleDegrees : closedAngleDegrees;
  Serial.printf("[SERVO] Jog Richtung %s\n", direction < 0 ? "auf" : "zu");
}

void ClawServoMotor::stopMoving()
{
  isJogging = false;
  targetAngleDegrees = currentAngleDegrees;
  Serial.printf("[SERVO] Gestoppt bei %.1f Grad\n", currentAngleDegrees);
}

void ClawServoMotor::open()
{
  setAngle(openAngleDegrees);
}

void ClawServoMotor::close()
{
  setAngle(closedAngleDegrees);
}
