#pragma once

#include <Adafruit_MotorShield.h>
#include <Arduino.h>

class ClawStepperMotor {
public:
  static const uint16_t STEPS_PER_REVOLUTION = 200;

  ClawStepperMotor(Adafruit_MotorShield &motorShield, uint8_t stepperPort, uint16_t maxRevolutionsPerMinute);

  void begin();
  void setSpeed(int speedPercent);
  void update();

private:
  Adafruit_MotorShield &motorShield;
  uint8_t stepperPort;
  uint16_t maxRevolutionsPerMinute;
  Adafruit_StepperMotor *stepperMotor;
  int currentSpeedPercent               = 0;
  unsigned long stepIntervalMicroseconds = 0;
  unsigned long lastStepMicroseconds     = 0;
};
