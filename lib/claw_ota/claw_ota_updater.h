#pragma once

#include <Arduino.h>

#include "claw_mqtt_connection.h"

// Firmware-Update ueber WLAN (OTA). Das Geraet wartet auf eine MQTT-Nachricht
//   clawmachine/<deviceName>/ota   mit dem Payload   {"url":"http://<broker>:<port>/firmware/<datei>.bin"}
// laedt die Datei per HTTP, flasht sie in den zweiten App-Slot und startet neu.
// Fortschritt/Ergebnis kommt als JSON auf clawmachine/<deviceName>/ota/status.
//
// Rollback: Nach einem Update gilt die neue Firmware erst als "pending". Sie wird
// erst bestaetigt, wenn WLAN + MQTT stehen und (falls gesetzt) die
// Hardware-Pruefung true liefert. Klappt das nicht innerhalb von
// VERIFICATION_TIMEOUT_MILLISECONDS oder resettet das Geraet vorher (Absturz,
// Stromausfall), startet der Bootloader wieder die vorherige Firmware. Diese
// meldet danach einmalig state "rolled_back" auf dem Status-Topic.
//
// Sicherheit: Die URL wird nur akzeptiert, wenn ihr Host dem MQTT-Broker-Host
// entspricht — wer MQTT-Zugangsdaten hat, kann so keine Firmware von einem
// beliebigen fremden Server einspielen.
class ClawOtaUpdater {
public:
  ClawOtaUpdater(
      ClawMqttConnection &connection,
      const char *deviceName,
      const char *allowedDownloadHost);

  // Abonniert das OTA-Topic. Nach connection.begin() aufrufen.
  void begin();

  // Wird vor dem Flashen aufgerufen, z.B. um Motoren zu stoppen.
  void setBeforeUpdateCallback(void (*callback)());

  // Optionale Pruefung, ob die Hardware der neuen Firmware funktioniert
  // (z.B. Motor-Shields antworten). Muss true liefern, damit bestaetigt wird.
  void setHealthCheck(bool (*check)());

  // In loop() aufrufen: bestaetigt die neue Firmware bzw. loest den Rollback aus.
  void maintain();

  // Im MQTT-Callback des Geraets als Erstes aufrufen. Liefert true, wenn die
  // Nachricht ein OTA-Befehl war (und damit hier verarbeitet wurde).
  bool handleMessage(const char *topic, const String &message);

private:
  static constexpr unsigned long VERIFICATION_TIMEOUT_MILLISECONDS = 60000;

  void publishStatus(const char *state, const String &detail);
  bool isAllowedUrl(const String &url) const;

  ClawMqttConnection &connection;
  const char *deviceName;
  const char *allowedDownloadHost;
  String otaTopic;
  String otaStatusTopic;
  void (*beforeUpdateCallback)() = nullptr;
  bool (*healthCheck)() = nullptr;
  bool isVerificationPending = false;
  bool hasReportedRollback = false;
};
