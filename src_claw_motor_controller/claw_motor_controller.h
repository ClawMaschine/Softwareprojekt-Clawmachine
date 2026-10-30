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
  // Nimmt die Referenz einer Achse zurueck: die gezaehlte Position gilt nicht mehr und die
  // Verfahrgrenzen werden aufgehoben. Notwendig vor dem Homing — sonst wuerde der Controller
  // die Achse an der alten unteren Grenze anhalten, bevor der Endstop ausloest.
  bool clearPositionReference(char axis);
  // Faehrt eine Achse mit speedPercent (Betrag) zur Position in Schritten und haelt dort an.
  bool moveToPosition(char axis, long steps, int speedPercent);
  // Verfahrweg einer Achse in Schritten (0 = unbegrenzt). Gilt erst, wenn die Achse
  // referenziert ist (zero:<Achse> empfangen); dann haelt der Controller die Achse selbst
  // bei 0 und beim Verfahrweg an, unabhaengig vom Server.
  bool setTravelSteps(char axis, long travelSteps);
  // Gibt die aktuell wirksamen Einstellungen jeder Achse seriell aus (Beschleunigung,
  // Drehzahl, Verfahrweg, Referenz, Position, Zielgeschwindigkeit).
  void printSettings();
  // Haelt alle Achsen sofort an und bricht Zielfahrten und Klauen-Jog ab (z.B. bei MQTT-Abbruch).
  void stopAllMotion();
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
  static int axisIndex(char axis);
  // Uebertraegt Verfahrweg und Referenzstatus einer Achse als Positionsfenster auf ihre
  // Motoren. Ab dort setzt der Schrittmotor die Grenze selbst durch — vor jedem Schritt.
  void applyTravelLimits(char axis);
  // true, wenn ein Motor der Achse seit dem letzten Aufruf an seiner Verfahrgrenze
  // angehalten wurde (holt die Meldung bei den Motoren ab und setzt sie zurueck).
  bool consumeTravelLimitHit(char axis);
  // Haelt die ganze Achse an, sobald einer ihrer Motoren an der Verfahrgrenze angehalten hat
  // (X hat zwei Motoren — laeuft einer weiter, arbeiten beide gegeneinander), und meldet das
  // einmalig je Ereignis. Nicht in jedem update()-Durchlauf protokollieren: jede serielle
  // Ausgabe blockiert bei 115200 Baud einige Millisekunden und damit die Schritte der Motoren.
  void handleTravelLimitHits();

  long travelStepsByAxis[3] = {2300, 0, 0};
  bool isAxisReferenced[3]  = {false, false, false};

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
