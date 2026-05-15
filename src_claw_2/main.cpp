#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SCAN_TIME 5   // Sekunden

BLEScan* pBLEScan;

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) override {
    Serial.println("----- Bluetooth Gerät gefunden -----");

    Serial.print("Name: ");
    if (device.haveName()) {
      Serial.println(device.getName().c_str());
    } else {
      Serial.println("(kein Name)");
    }

    Serial.print("MAC: ");
    Serial.println(device.getAddress().toString().c_str());

    Serial.print("RSSI: ");
    Serial.println(device.getRSSI());

    Serial.println("------------------------------------");
  }
};

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("ESP32 Bluetooth Scanner startet...");

  BLEDevice::init("");

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void loop() {
  Serial.println("Scan läuft...");

  BLEScanResults results = pBLEScan->start(SCAN_TIME, false);

  Serial.print("Gefundene Geräte: ");
  Serial.println(results.getCount());

  pBLEScan->clearResults();

  delay(2000);
}