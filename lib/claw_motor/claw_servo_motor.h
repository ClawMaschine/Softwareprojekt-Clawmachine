#pragma once

#include <Arduino.h>
#include <ESP32Servo.h>

class ClawServoMotor {
public:
  explicit ClawServoMotor(uint8_t servoPin, uint8_t openAngleDegrees = MIN_ALLOWED_ANGLE_DEGREES,
                          uint8_t closedAngleDegrees = MAX_ALLOWED_ANGLE_DEGREES);

  void begin();
  void open();
  void close();
  // Zielposition in Grad, begrenzt auf den erlaubten Fahrbereich der Klaue
  // (MIN_/MAX_ALLOWED_ANGLE_DEGREES). Die Klaue faehrt mit der eingestellten
  // Geschwindigkeit dorthin; dazu muss update() regelmaessig aufgerufen werden.
  void setAngle(uint8_t angleDegrees);
  // Konstante Fahrgeschwindigkeit in Grad pro Sekunde (keine Beschleunigungsrampe).
  void setSpeed(float degreesPerSecond);
  void update();

  // Fahrbereich, den die Klaue mechanisch nutzen darf (am Aufbau ermittelt mit
  // src_esp_test). Winkel ausserhalb werden abgeschnitten.
  static const uint8_t MIN_ALLOWED_ANGLE_DEGREES = 90;
  static const uint8_t MAX_ALLOWED_ANGLE_DEGREES = 160;

private:
  // 50 Hz, Pulsbreite 700-2300 us entspricht 0-180 Grad bei Servo::write().
  // Der volle Standardbereich (500-2400 us) laesst den Servo rattern.
  static const int SERVO_PERIOD_HERTZ = 50;
  static const int SERVO_MIN_PULSE_MICROSECONDS = 700;
  static const int SERVO_MAX_PULSE_MICROSECONDS = 2300;

  static constexpr float DEFAULT_SPEED_DEGREES_PER_SECOND = 35.0f;
  static constexpr float MIN_SPEED_DEGREES_PER_SECOND = 1.0f;

  void writeAngle(float angleDegrees);

  float currentAngleDegrees;
  float targetAngleDegrees;
  float speedDegreesPerSecond = DEFAULT_SPEED_DEGREES_PER_SECOND;
  unsigned long lastUpdateMicroseconds = 0;

  Servo servo;
  uint8_t servoPin;
  uint8_t openAngleDegrees;
  uint8_t closedAngleDegrees;
};
