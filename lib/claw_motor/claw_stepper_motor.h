#pragma once

#include <Adafruit_MotorShield.h>
#include <Arduino.h>

class ClawStepperMotor {
public:
  static const uint16_t STEPS_PER_REVOLUTION = 200;

  ClawStepperMotor(Adafruit_MotorShield &motorShield, uint8_t stepperPort, uint16_t maxRevolutionsPerMinute);

  void begin();
  void setSpeed(int speedPercent);

  // Wie schnell sich die tatsächliche Geschwindigkeit pro Sekunde Richtung
  // Zielgeschwindigkeit bewegen darf, in Prozentpunkten/Sekunde (z.B. 200
  // bedeutet: von 0% auf 100% in 0.5s). 0 (Standard) = keine Rampe, die
  // Geschwindigkeit springt wie bisher sofort auf den Zielwert.
  void setAcceleration(float percentPerSecond);

  void update();

private:
  void applySpeed(float speedPercent);
  void updateRamp();

  Adafruit_MotorShield &motorShield;
  uint8_t stepperPort;
  uint16_t maxRevolutionsPerMinute;
  Adafruit_StepperMotor *stepperMotor;

  int targetSpeedPercent = 0;

  // Float statt int: update()/updateRamp() kann sehr oft pro Sekunde
  // aufgerufen werden (loop() hat kein delay() mehr, siehe main.cpp), da
  // müssen sich kleine Bruchteile über viele Aufrufe hinweg korrekt
  // aufsummieren können, statt bei jedem Aufruf auf mindestens 1
  // Prozentpunkt aufgerundet zu werden — sonst ist die Rampe in wenigen
  // Millisekunden durch, egal welche Beschleunigung eingestellt ist.
  float currentSpeedPercent              = 0;
  float accelerationPercentPerSecond     = 0;
  unsigned long stepIntervalMicroseconds = 0;
  unsigned long lastStepMicroseconds     = 0;
  unsigned long lastRampMicroseconds     = 0;
};
