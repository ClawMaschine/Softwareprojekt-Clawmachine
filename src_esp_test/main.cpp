#include <Arduino.h>

#include "claw_servo_motor.h"
#include "firmware_config.h"

// Reiner Hardware-Test ohne WiFi/MQTT: faehrt den Klauen-Servo an
// CLAW_CLAW_SERVO_PIN in kleinen Schritten langsam auf und zu, in Dauerschleife.

namespace {
constexpr uint8_t SWEEP_MIN_ANGLE_DEGREES = 0;
constexpr uint8_t SWEEP_MAX_ANGLE_DEGREES = 180;
constexpr uint8_t SWEEP_STEP_DEGREES = 1;
constexpr unsigned long SWEEP_STEP_DELAY_MS = 25; // groesser = langsamere Fahrt
}

ClawServoMotor testServo(CLAW_CLAW_SERVO_PIN);

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("[ESP_TEST] Servo-Test auf GPIO %d, langsamer Sweep %u-%u Grad\n",
                CLAW_CLAW_SERVO_PIN, SWEEP_MIN_ANGLE_DEGREES, SWEEP_MAX_ANGLE_DEGREES);
  testServo.begin();
}

void loop()
{
  for (int angle = SWEEP_MIN_ANGLE_DEGREES; angle <= SWEEP_MAX_ANGLE_DEGREES; angle += SWEEP_STEP_DEGREES) {
    testServo.setAngle(static_cast<uint8_t>(angle));
    delay(SWEEP_STEP_DELAY_MS);
  }
  for (int angle = SWEEP_MAX_ANGLE_DEGREES; angle >= SWEEP_MIN_ANGLE_DEGREES; angle -= SWEEP_STEP_DEGREES) {
    testServo.setAngle(static_cast<uint8_t>(angle));
    delay(SWEEP_STEP_DELAY_MS);
  }
}
