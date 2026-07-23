#pragma once

#include <Adafruit_MotorShield.h>
#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "claw_servo_motor.h"
#include "claw_stepper_motor.h"

class ClawMotorController {
public:
  static constexpr const char *COMMAND_TOPIC = "clawmachine/motor_controller/command";

  ClawMotorController(
      ClawMqttConnection &connection,
      uint8_t motorShieldAI2cAddress,
      uint8_t motorShieldBI2cAddress,
      uint16_t maxRevolutionsPerMinute,
      uint8_t clawServoPin);

  void begin();
  void update();

private:
  static void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);

  void move(char axis, int speed);
  void moveZ(int speed);
  void moveClaw(const char *command);

  static ClawMotorController *instance;
  ClawMqttConnection &connection;

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
