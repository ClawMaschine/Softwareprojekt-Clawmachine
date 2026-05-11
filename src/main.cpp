#include <Arduino.h>
#include <BluetoothSerial.h>

BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  // Bluetooth mit Namen "ESP32-BT" aktivieren
  SerialBT.begin("ESP32-BT");
  Serial.println("Bluetooth gestartet. Suche nach: ESP32-BT");
}

void loop() {
  // Daten vom Bluetooth empfangen und zum Serial Monitor senden
  if (SerialBT.available()) {
    Serial.write(SerialBT.read());
  }
  
  // Daten vom Serial Monitor empfangen und zum Bluetooth senden
  if (Serial.available()) {
    SerialBT.write(Serial.read());
  }
  
  delay(20);
}