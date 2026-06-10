#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

class ClawMqttConnection {
public:
  ClawMqttConnection(
      const char *wifiSsid,
      const char *wifiPassword,
      const char *mqttBrokerHost,
      uint16_t mqttBrokerPort,
      const char *mqttClientId,
      const char *mqttUsername,
      const char *mqttPassword,
      unsigned long reconnectIntervalMilliseconds);

  void begin();
  void maintainConnection();

private:
  bool ensureWifiConnected();
  bool ensureMqttConnected();

  const char *wifiSsid;
  const char *wifiPassword;
  const char *mqttBrokerHost;
  uint16_t mqttBrokerPort;
  const char *mqttClientId;
  const char *mqttUsername;
  const char *mqttPassword;
  unsigned long reconnectIntervalMilliseconds;
  unsigned long lastWifiConnectAttemptMilliseconds;
  unsigned long lastMqttConnectAttemptMilliseconds;
  WiFiClient networkClient;
  PubSubClient mqttClient;
};
