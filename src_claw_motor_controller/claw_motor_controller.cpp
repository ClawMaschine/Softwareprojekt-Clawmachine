#include "claw_motor_controller.h"

ClawMotorController *ClawMotorController::instance = nullptr;

ClawMotorController::ClawMotorController(ClawMqttConnection &connection)
    : connection(connection)
{
  instance = this;
}

void ClawMotorController::begin()
{
  connection.setMessageCallback(onMqttMessage);
  connection.subscribe(COMMAND_TOPIC);
}

void ClawMotorController::onMqttMessage(char *topic, uint8_t *payload, unsigned int length)
{
  if (instance == nullptr) {
    return;
  }

  char payloadStr[length + 1];
  memcpy(payloadStr, payload, length);
  payloadStr[length] = '\0';

  if (strncmp(payloadStr, "x:", 2) == 0) {
    instance->move('x', atoi(payloadStr + 2));
  } else if (strncmp(payloadStr, "y:", 2) == 0) {
    instance->move('y', atoi(payloadStr + 2));
  } else if (strncmp(payloadStr, "z:", 2) == 0) {
    instance->moveZ(constrain(atoi(payloadStr + 2), -100, 100));
  } else if (strncmp(payloadStr, "claw:", 5) == 0) {
    instance->moveClaw(constrain(atoi(payloadStr + 5), -100, 100));
  } else {
    Serial.print("[MOTOR] Unknown command: ");
    Serial.println(payloadStr);
  }
}

void ClawMotorController::move(char axis, int speed)
{
  switch (axis) {
    case 'x':
      currentX = speed;
      Serial.printf("[MOTOR] X: %d\n", speed);
      // TODO: GPIO motor control
      break;
    case 'y':
      currentY = speed;
      Serial.printf("[MOTOR] Y: %d\n", speed);
      // TODO: GPIO motor control
      break;
  }
}

void ClawMotorController::moveZ(int speed)
{
  ropeSpeed = speed;
  Serial.printf("[MOTOR] Z (Seil): %d\n", speed);
  // TODO: GPIO motor control
}

void ClawMotorController::moveClaw(int speed)
{
  clawSpeed = speed;
  Serial.printf("[MOTOR] Klaue: %d\n", speed);
  // TODO: GPIO motor control
}
