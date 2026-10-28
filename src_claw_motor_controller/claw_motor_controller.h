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
  // Position einer Achse in Schritten, im Koordinatensystem des Servers: 0 = Endstop,
  // positive Werte = vom Endstop weg (gleiche Richtung wie positive Geschwindigkeit).
  long getPosition(char axis) const;
  // Setzt die aktuelle Stellung der Achse als Position 0 (Referenzieren am Endstop).
  bool zeroPosition(char axis);
  // Faehrt eine Achse mit speedPercent (Betrag) zur Position in Schritten und haelt dort an.
  bool moveToPosition(char axis, long steps, int speedPercent);
  // Beschleunigung in Prozentpunkten Geschwindigkeit pro Sekunde (0 = keine Rampe),
  // getrennt fuer die X/Y-Achsen und die Z-Achse. setAcceleration() setzt beide.
  void setAcceleration(float percentPerSecond);
  void setXyAcceleration(float percentPerSecond);
  void setZAcceleration(float percentPerSecond);
  // Drehzahl in Umdrehungen pro Minute, die 100 % Geschwindigkeit entspricht,
  // getrennt fuer die X/Y-Achsen und die Z-Achse.
  void setXyMaxRevolutionsPerMinute(uint16_t revolutionsPerMinute);
  void setZMaxRevolutionsPerMinute(uint16_t revolutionsPerMinute);
  // Fahrgeschwindigkeit der Klaue in Grad pro Sekunde (konstant, ohne Rampe)
  void setClawSpeed(float degreesPerSecond);
  // Geschwindigkeit fuer das langsame Fahren bei gehaltener Taste (claw:jog_open/jog_close)
  void setClawJogSpeed(float degreesPerSecond);

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
