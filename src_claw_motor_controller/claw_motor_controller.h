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
  void setAcceleration(float percentPerSecond);

private:


  static ClawMotorController *instance;
  ClawMqttConnection &connection;

  uint8_t motorShieldAI2cAddress;
  uint8_t motorShieldBI2cAddress;
  float accelerationPercentPerSecond;
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
