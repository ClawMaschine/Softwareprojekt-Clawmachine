#include <Arduino.h>

#include "firmware_config.h"

// Reiner Hardware-Test ohne WiFi/MQTT und ohne Servo-Bibliothek: gibt am
// Klauen-Servo-Pin ein 50-Hz-PWM-Signal per analogWrite mit einfachen Rohwerten (Duty) aus und
// faehrt die Duty in kleinen Schritten langsam hoch und runter, in Dauerschleife.

namespace {
constexpr uint32_t PWM_FREQUENCY_HZ = 50;
constexpr uint8_t PWM_RESOLUTION_BITS = 14; // Duty 0-16383 entspricht 0-20 ms
// 1 ms = 819, 1,5 ms = 1229, 2 ms = 1638
constexpr uint32_t SWEEP_MIN_DUTY = 819;
constexpr uint32_t SWEEP_MAX_DUTY = 1638;
constexpr uint32_t SWEEP_STEP_DUTY = 5;
constexpr unsigned long SWEEP_STEP_DELAY_MS = 25; // groesser = langsamere Fahrt
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("[ESP_TEST] PWM-Test auf GPIO %d, %lu Hz, Duty %lu-%lu\n",
                CLAW_CLAW_SERVO_PIN, PWM_FREQUENCY_HZ, SWEEP_MIN_DUTY, SWEEP_MAX_DUTY);
  analogWriteResolution(CLAW_CLAW_SERVO_PIN, PWM_RESOLUTION_BITS);
  analogWriteFrequency(CLAW_CLAW_SERVO_PIN, PWM_FREQUENCY_HZ);
}

void loop()
{
  for (uint32_t duty = SWEEP_MIN_DUTY; duty <= SWEEP_MAX_DUTY; duty += SWEEP_STEP_DUTY) {
    analogWrite(CLAW_CLAW_SERVO_PIN, duty);
    delay(SWEEP_STEP_DELAY_MS);
  }
  for (uint32_t duty = SWEEP_MAX_DUTY; duty >= SWEEP_MIN_DUTY; duty -= SWEEP_STEP_DUTY) {
    analogWrite(CLAW_CLAW_SERVO_PIN, duty);
    delay(SWEEP_STEP_DELAY_MS);
  }
}
