#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

class ClawServoMotor {
public:
  explicit ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees = 0, uint8_t closedAngleDegrees = 180);

  void begin();
  void open();
  void close();
  // Freie Position in Grad (0-180), zum Testen/Feinjustieren der Klaue.
  void setAngle(uint8_t angleDegrees);

private:
  // Standard-Servo (z.B. SG90/MG90): 50 Hz, Pulsbreite 500-2400 us.
  static const int SERVO_PERIOD_HERTZ = 50;
  static const int SERVO_MIN_PULSE_MICROSECONDS = 500;
  static const int SERVO_MAX_PULSE_MICROSECONDS = 2400;

  Servo servo;
  uint8_t servoPin;
  uint8_t openAngleDegrees;
  uint8_t closedAngleDegrees;
};
