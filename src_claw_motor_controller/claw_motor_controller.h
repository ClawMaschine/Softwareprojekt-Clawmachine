#pragma once

#include <Arduino.h>

#include "claw_mqtt_connection.h"

class ClawMotorController {
public:
  static constexpr const char *COMMAND_TOPIC = "clawmachine/motor_controller/command";

  explicit ClawMotorController(ClawMqttConnection &connection);
  void begin();

private:
  static void onMqttMessage(char *topic, uint8_t *payload, unsigned int length);

  void move(char axis, int speed);
  void moveZ(int speed);
  void moveClaw(int speed);

  static ClawMotorController *instance;
  ClawMqttConnection &connection;
  int currentX  = 0;
  int currentY  = 0;
  int ropeSpeed = 0;
  int clawSpeed = 0;
};
