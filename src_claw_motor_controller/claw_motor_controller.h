#pragma once

#include <Adafruit_MotorShield.h>
#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "claw_servo_motor.h"
#include "claw_stepper_motor.h"

class ClawMotorController {
public:
  ClawMotorController(
      ClawMqttConnection &connection,
      uint8_t motorShieldAI2cAddress,
      uint8_t motorShieldBI2cAddress,
      uint16_t maxRevolutionsPerMinute,
      uint8_t clawServoPin,
      float accelerationPercentPerSecond = 0);

  void begin();
  void update();

  // Liefern false, wenn Achse bzw. Klauen-Befehl unbekannt ist.
  bool move(char axis, int speed);
  bool moveClaw(const char *command);
  // Haelt eine Achse sofort an (ohne Beschleunigungsrampe), z.B. am Endstop.
  bool stopAxisImmediately(char axis);
  // Beschleunigung in Prozentpunkten Geschwindigkeit pro Sekunde (0 = keine Rampe),
  // getrennt fuer die X/Y-Achsen und die Z-Achse. setAcceleration() setzt beide.
  void setAcceleration(float percentPerSecond);
  void setXyAcceleration(float percentPerSecond);
  void setZAcceleration(float percentPerSecond);
  // Fahrgeschwindigkeit der Klaue in Grad pro Sekunde (konstant, ohne Rampe)
  void setClawSpeed(float degreesPerSecond);

private:


  static ClawMotorController *instance;
  ClawMqttConnection &connection;

  uint8_t motorShieldAI2cAddress;
  uint8_t motorShieldBI2cAddress;
  float xyAccelerationPercentPerSecond;
  float zAccelerationPercentPerSecond;
  Adafruit_MotorShield motorShieldA;
  Adafruit_MotorShield motorShieldB;

  // X wird von zwei Motoren auf getrennten Shields angetrieben (Shield A links, Shield B rechts).
  ClawStepperMotor xMotorLeft;
  ClawStepperMotor yMotor;
  ClawStepperMotor xMotorRight;
  ClawStepperMotor zMotor;
  ClawServoMotor clawServo;

  int currentX  = 0;
  int currentY  = 0;
  int ropeSpeed = 0;
};
