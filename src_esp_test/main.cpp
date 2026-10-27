#include <Arduino.h>
#include <ESP32Servo.h>

#include "firmware_config.h"

// Hardware-Test ohne WiFi/MQTT zum Einstellen des Klauen-Servos: Winkel ueber die
// serielle Konsole vorgeben und an der Klaue ablesen, in welchem Bereich sie fahren darf.
//
// Eingaben (Zeile mit Enter abschicken):
//   <Zahl 0-180>   Winkel direkt anfahren, z.B. "90"
//   +  /  -        Winkel um 5 Grad erhoehen / verringern
//   ++ /  --       Winkel um 1 Grad erhoehen / verringern
//   0 und 180 sind die Endpunkte des Servo-Bereichs (700-2300 us).

namespace {
constexpr int SERVO_PERIOD_HERTZ = 50;
constexpr int SERVO_MIN_PULSE_MICROSECONDS = 700;
constexpr int SERVO_MAX_PULSE_MICROSECONDS = 2300;
constexpr int SERVO_FULL_RANGE_MAX_ANGLE_DEGREES = 180; // Winkel, auf den write() 700-2300 us abbildet
constexpr int MIN_ANGLE_DEGREES = 90;
constexpr int MAX_ANGLE_DEGREES = 160;
constexpr int START_ANGLE_DEGREES = 90;
constexpr int LARGE_STEP_DEGREES = 5;
constexpr int SMALL_STEP_DEGREES = 1;
}

Servo testServo;
int currentAngleDegrees = START_ANGLE_DEGREES;
String receivedLine;

void moveToAngle(int angleDegrees)
{
  currentAngleDegrees = constrain(angleDegrees, MIN_ANGLE_DEGREES, MAX_ANGLE_DEGREES);
  testServo.write(currentAngleDegrees);
  Serial.printf("[ESP_TEST] Winkel: %d Grad (%d us)\n", currentAngleDegrees,
                map(currentAngleDegrees, 0, SERVO_FULL_RANGE_MAX_ANGLE_DEGREES,
                    SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS));
}

void handleCommand(String command)
{
  command.trim();
  if (command.isEmpty()) {
    return;
  }
  if (command == "+") {
    moveToAngle(currentAngleDegrees + LARGE_STEP_DEGREES);
  } else if (command == "-") {
    moveToAngle(currentAngleDegrees - LARGE_STEP_DEGREES);
  } else if (command == "++") {
    moveToAngle(currentAngleDegrees + SMALL_STEP_DEGREES);
  } else if (command == "--") {
    moveToAngle(currentAngleDegrees - SMALL_STEP_DEGREES);
  } else if (command.toInt() != 0 || command == "0") {
    moveToAngle(command.toInt());
  } else {
    Serial.printf("[ESP_TEST] Unbekannte Eingabe: %s\n", command.c_str());
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.printf("[ESP_TEST] Servo-Einstellung auf GPIO %d, %d Hz, %d-%d us\n",
                CLAW_CLAW_SERVO_PIN, SERVO_PERIOD_HERTZ, SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS);
  Serial.println("[ESP_TEST] Eingabe: Zahl 0-180, + / - (5 Grad), ++ / -- (1 Grad)");
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  testServo.setPeriodHertz(SERVO_PERIOD_HERTZ);
  testServo.attach(CLAW_CLAW_SERVO_PIN, SERVO_MIN_PULSE_MICROSECONDS, SERVO_MAX_PULSE_MICROSECONDS);
  if (!testServo.attached()) {
    Serial.println("[ESP_TEST] FEHLER: attach fehlgeschlagen");
    return;
  }
  moveToAngle(START_ANGLE_DEGREES);
}

void loop()
{
  while (Serial.available() > 0) {
    char receivedCharacter = static_cast<char>(Serial.read());
    if (receivedCharacter == '\n' || receivedCharacter == '\r') {
      handleCommand(receivedLine);
      receivedLine = "";
    } else {
      receivedLine += receivedCharacter;
    }
  }
}
