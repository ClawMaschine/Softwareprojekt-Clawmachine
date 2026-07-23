#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

class ClawServoMotor {
public:
  explicit ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees = 0, uint8_t closedAngleDegrees = 180);

  void begin();
  void open();
  void close();

private:
  Servo servo;
  uint8_t servoPin;
  uint8_t openAngleDegrees;
  uint8_t closedAngleDegrees;
};
