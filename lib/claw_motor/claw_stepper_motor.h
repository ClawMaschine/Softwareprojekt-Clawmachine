#pragma once

#include <Adafruit_MotorShield.h>
#include <Arduino.h>

class ClawStepperMotor {
public:
  static const uint16_t STEPS_PER_REVOLUTION = 200;

  ClawStepperMotor(Adafruit_MotorShield &motorShield, uint8_t stepperPort, uint16_t maxRevolutionsPerMinute);

  void begin();
  void setSpeed(int speedPercent);
  // Haelt den Motor sofort an, ohne die Beschleunigungsrampe herunterzufahren
  // (z.B. am Endstop). Das Anfahren bleibt davon unberuehrt und laeuft weiter ueber die Rampe.
  void stopImmediately();

  // Wie schnell sich die tatsächliche Geschwindigkeit pro Sekunde Richtung
  // Zielgeschwindigkeit bewegen darf, in Prozentpunkten/Sekunde (z.B. 200
  // bedeutet: von 0% auf 100% in 0.5s). 0 (Standard) = keine Rampe, die
  // Geschwindigkeit springt wie bisher sofort auf den Zielwert.
  void setAcceleration(float percentPerSecond);
  // Drehzahl, die 100 % Geschwindigkeit entspricht. Wirkt sofort auch auf eine laufende Fahrt.
  void setMaxRevolutionsPerMinute(uint16_t newMaxRevolutionsPerMinute);
  // Zaehlt jeden ausgefuehrten Schritt mit Vorzeichen (vorwaerts = +1, rueckwaerts = -1).
  // Es gibt keine Rueckmeldung vom Motor: verlorene Schritte fallen nicht auf, die Position
  // stimmt nur bis zum naechsten Referenzieren (resetStepPosition am Endstop).
  long getStepPosition() const;
  float getAcceleration() const;
  uint16_t getMaxRevolutionsPerMinute() const;
  // Zuletzt vorgegebene Zielgeschwindigkeit in % mit Vorzeichen (unabhaengig von der Rampe).
  int getTargetSpeedPercent() const;
  void resetStepPosition(long newStepPosition);
  // Faehrt mit der angegebenen Geschwindigkeit (Betrag in %, Richtung ergibt sich aus
  // Ziel - aktuelle Position) bis zur Zielposition und haelt dort sofort an. Jeder
  // setSpeed()/stopImmediately()-Aufruf bricht die Zielfahrt ab. Ein Ziel ausserhalb
  // des mit setStepPositionLimits() gesetzten Fensters wird auf dessen Rand begrenzt.
  void moveToStepPosition(long targetStepPositionValue, int speedPercent);

  // Begrenzt die Schrittposition auf [minStepPosition, maxStepPosition]. Der Motor fuehrt
  // keinen Schritt aus, der aus dem Fenster herausfuehren wuerde — geprueft wird vor jedem
  // einzelnen Schritt und nicht nur bei jedem Fahrbefehl. Damit kann weder ein wiederholter
  // setSpeed()-Aufruf noch eine noch auslaufende Bremsrampe die Grenze ueberfahren.
  // Die Grenzen gelten in der Zaehlrichtung dieses Motors (getStepPosition()).
  void setStepPositionLimits(long minStepPosition, long maxStepPosition);
  void clearStepPositionLimits();
  // true, wenn der Motor seit dem letzten Aufruf an einer Grenze angehalten wurde. Der Aufruf
  // setzt die Meldung zurueck: ein Ereignis wird so nur einmal gemeldet. Abfragen statt direkt
  // protokollieren, weil update() pro Sekunde sehr oft laeuft und kein Serial enthalten darf.
  bool consumeStepPositionLimitHit();

  void update();

private:
  void applySpeed(float speedPercent);
  void updateRamp();
  // true, solange der naechste Schritt in Fahrtrichtung das Fenster verlassen wuerde.
  bool isBlockedByStepPositionLimit() const;
  Adafruit_MotorShield &motorShield;
  uint8_t stepperPort;
  uint16_t maxRevolutionsPerMinute;
  Adafruit_StepperMotor *stepperMotor;

  int targetSpeedPercent = 0;

  long stepPosition = 0;
  bool hasTargetStepPosition = false;
  long targetStepPosition = 0;

  // Erlaubtes Positionsfenster (siehe setStepPositionLimits()). Ohne gesetzte Grenzen
  // faehrt der Motor unbegrenzt weiter, wie vor dieser Erweiterung.
  bool hasStepPositionLimits = false;
  long minStepPosition       = 0;
  long maxStepPosition       = 0;
  // Gemerkt bis zum Abholen durch consumeStepPositionLimitHit(): der Stopp an der Grenze
  // setzt die Geschwindigkeit auf 0 und ist danach von aussen nicht mehr zu erkennen.
  bool hasHitStepPositionLimit = false;

  // Float statt int: update()/updateRamp() kann sehr oft pro Sekunde
  // aufgerufen werden (loop() hat kein delay() mehr, siehe main.cpp), da
  // müssen sich kleine Bruchteile über viele Aufrufe hinweg korrekt
  // aufsummieren können, statt bei jedem Aufruf auf mindestens 1
  // Prozentpunkt aufgerundet zu werden — sonst ist die Rampe in wenigen
  // Millisekunden durch, egal welche Beschleunigung eingestellt ist.
  float currentSpeedPercent              = 0;
  float accelerationPercentPerSecond     = 0;
  unsigned long stepIntervalMicroseconds = 0;
  unsigned long lastStepMicroseconds     = 0;
  unsigned long lastRampMicroseconds     = 0;

  // Verhindert, dass release() bei jedem update()-Aufruf erneut per I2C
  // gesendet wird, solange der Motor bereits stromlos ist.
  bool isStepperReleased = false;
};
