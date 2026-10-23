#include "claw_ota_updater.h"

#include <ArduinoJson.h>
#include <HTTPUpdate.h>
#include <WiFi.h>
#include <esp_ota_ops.h>

// Ueberschreibt die schwache Funktion aus dem Arduino-Core: Der Core wuerde die
// neue Firmware sonst direkt beim Start als gueltig markieren. Hier bestaetigen
// wir sie erst in ClawOtaUpdater::maintain(). Nur einmal pro Firmware definieren
// (diese Datei gehoert zu jeder Firmware, die ClawOtaUpdater nutzt).
extern "C" bool verifyRollbackLater()
{
  return true;
}

ClawOtaUpdater::ClawOtaUpdater(
    ClawMqttConnection &connection,
    const char *deviceName,
    const char *allowedDownloadHost)
    : connection(connection),
      deviceName(deviceName),
      allowedDownloadHost(allowedDownloadHost),
      otaTopic(String("clawmachine/") + deviceName + "/ota"),
      otaStatusTopic(String("clawmachine/") + deviceName + "/ota/status")
{
}

void ClawOtaUpdater::begin()
{
  connection.subscribe(otaTopic.c_str());

  esp_ota_img_states_t imageState;
  const esp_partition_t *runningPartition = esp_ota_get_running_partition();
  isVerificationPending = esp_ota_get_state_partition(runningPartition, &imageState) == ESP_OK &&
                          imageState == ESP_OTA_IMG_PENDING_VERIFY;
  if (isVerificationPending) {
    Serial.println("[OTA] Neue Firmware wartet auf Bestaetigung (WLAN + MQTT + Hardware-Check)");
  }
}

void ClawOtaUpdater::setHealthCheck(bool (*check)())
{
  healthCheck = check;
}

void ClawOtaUpdater::maintain()
{
  if (isVerificationPending) {
    if (connection.isMqttConnected() && (healthCheck == nullptr || healthCheck())) {
      esp_ota_mark_app_valid_cancel_rollback();
      isVerificationPending = false;
      publishStatus("confirmed", "Neue Firmware bestaetigt");
    } else if (millis() > VERIFICATION_TIMEOUT_MILLISECONDS) {
      Serial.println("[OTA] Bestaetigung fehlgeschlagen -> Rollback");
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    return;
  }

  // Nach einem Rollback laeuft wieder die alte Firmware; die verworfene
  // Partition merkt sich das Betriebssystem. Einmal melden, sobald MQTT steht.
  if (!hasReportedRollback && connection.isMqttConnected()) {
    hasReportedRollback = true;
    const esp_partition_t *invalidPartition = esp_ota_get_last_invalid_partition();
    if (invalidPartition != nullptr) {
      publishStatus("rolled_back", String("Update verworfen (Partition ") + invalidPartition->label + ")");
    }
  }
}

void ClawOtaUpdater::setBeforeUpdateCallback(void (*callback)())
{
  beforeUpdateCallback = callback;
}

void ClawOtaUpdater::publishStatus(const char *state, const String &detail)
{
  JsonDocument jsonDoc;
  jsonDoc["device"] = deviceName;
  jsonDoc["state"]  = state;
  jsonDoc["detail"] = detail;

  String statusJson;
  serializeJson(jsonDoc, statusJson);
  connection.publish(otaStatusTopic.c_str(), statusJson.c_str());
  Serial.printf("[OTA] %s\n", statusJson.c_str());
}

// Erwartet http://<host>[:<port>]/<pfad> — https wird nicht unterstuetzt.
bool ClawOtaUpdater::isAllowedUrl(const String &url) const
{
  const String scheme = "http://";
  if (!url.startsWith(scheme)) {
    return false;
  }
  const int hostStart = scheme.length();
  int hostEnd = url.indexOf('/', hostStart);
  if (hostEnd < 0) {
    return false;
  }
  String hostAndPort = url.substring(hostStart, hostEnd);
  const int portSeparator = hostAndPort.indexOf(':');
  const String host = portSeparator < 0 ? hostAndPort : hostAndPort.substring(0, portSeparator);
  return host == allowedDownloadHost;
}

bool ClawOtaUpdater::handleMessage(const char *topic, const String &message)
{
  if (otaTopic != topic) {
    return false;
  }

  JsonDocument jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, message);
  if (error) {
    publishStatus("error", String("JSON ungueltig: ") + error.c_str());
    return true;
  }

  const String url = jsonDoc["url"] | "";
  if (url.isEmpty()) {
    publishStatus("error", "Feld 'url' fehlt");
    return true;
  }
  if (!isAllowedUrl(url)) {
    publishStatus("error", String("URL nicht erlaubt (nur http://") + allowedDownloadHost + "/...)");
    return true;
  }

  publishStatus("started", url);
  if (beforeUpdateCallback != nullptr) {
    beforeUpdateCallback();
  }

  WiFiClient downloadClient;
  httpUpdate.rebootOnUpdate(false);
  t_httpUpdate_return result = httpUpdate.update(downloadClient, url);

  switch (result) {
    case HTTP_UPDATE_OK:
      publishStatus("success", "Neustart");
      delay(300); // Status-Nachricht noch rausschicken lassen
      ESP.restart();
      break;
    case HTTP_UPDATE_NO_UPDATES:
      publishStatus("error", "Server meldet kein Update");
      break;
    case HTTP_UPDATE_FAILED:
    default:
      publishStatus("error", httpUpdate.getLastErrorString());
      break;
  }
  return true;
}
