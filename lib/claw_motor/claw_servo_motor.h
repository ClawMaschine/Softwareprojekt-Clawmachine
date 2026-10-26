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
  // 50 Hz, Pulsbreite wie im Original-Greifer (Greifer_Komplett.ino): auf = 1320 us,
  // zu = 1790 us. Ein groesserer Bereich (Standard-Servo 500-2400 us) laesst den
  // Servo am Greifer anschlagen.
  static const int SERVO_PERIOD_HERTZ = 50;
  static const int SERVO_MIN_PULSE_MICROSECONDS = 1320;
  static const int SERVO_MAX_PULSE_MICROSECONDS = 1790;

  Servo servo;
  uint8_t servoPin;
  uint8_t openAngleDegrees;
  uint8_t closedAngleDegrees;
};
