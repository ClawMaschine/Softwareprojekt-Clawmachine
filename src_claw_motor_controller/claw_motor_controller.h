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
      uint8_t clawServoPin,
      float accelerationPercentPerSecond = 0);

  void begin();
  void update();

  void move(char axis, int speed);
  void moveZ(int speed);
  void moveClaw(const char *command);
  void setAcceleration(float percentPerSecond);

  void homing(char axis);
  void setEndstopXTriggered(bool isTriggered);
  void setEndstopYTriggered(bool isTriggered);
  void setEndstopZTriggered(bool isTriggered);

private:


  static ClawMotorController *instance;
  ClawMqttConnection &connection;

  // Von onMqttMessage() (main.cpp) per setEndstopXTriggered()/setEndstopYTriggered()
  // gesetzt, sobald clawmachine/motor_controller/endstop (vom Server weitergeleitet)
  // meldet, dass der jeweilige Endstop ausgeloest hat. volatile, da aus dem
  // MQTT-Callback heraus geschrieben und in homing()'s Warteschleife gelesen wird.
  volatile bool isEndstopXTriggered = false;
  volatile bool isEndstopYTriggered = false;
  volatile bool isEndstopZTriggered = false;

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
