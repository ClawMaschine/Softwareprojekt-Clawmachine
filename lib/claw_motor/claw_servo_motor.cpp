#include "claw_servo_motor.h"

ClawServoMotor::ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees, uint8_t closedAngleDegrees)
    : servoPin(servoPin), openAngleDegrees(openAngleDegrees), closedAngleDegrees(closedAngleDegrees)
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

  if (attachedChannel == 0 || !servo.attached()) {
    Serial.printf("[SERVO] FEHLER: attach auf GPIO %u fehlgeschlagen (Pin nicht PWM-faehig oder keine Timer frei)\n", servoPin);
  } else {
    Serial.printf("[SERVO] GPIO %u, LEDC-Kanal %d, %d-%d us, %d Hz\n", servoPin, attachedChannel,
                  SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS, SERVO_PERIOD_HERTZ);
  }
}

void ClawServoMotor::moveToAngle(uint8_t angleDegrees)
{
  if (!servo.attached()) {
    Serial.println("[SERVO] FEHLER: Servo nicht angehaengt, Befehl ignoriert");
    return;
  }
  servo.write(angleDegrees);
  Serial.printf("[SERVO] -> %u Grad\n", angleDegrees);
}

void ClawServoMotor::open()
{
  moveToAngle(openAngleDegrees);
}

void ClawServoMotor::close()
{
  moveToAngle(closedAngleDegrees);
}
