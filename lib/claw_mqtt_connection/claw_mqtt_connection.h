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
  void setMessageCallback(void (*callback)(char *, uint8_t *, unsigned int));
  void subscribe(const char *topic);
  void publish(const char *topic, const char *payload);

private:
  bool ensureWifiConnected();
  bool ensureMqttConnected();
  void resubscribeAll();
  void publishUptimeIfDue();

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
  unsigned long lastUptimePublishMilliseconds;
  WiFiClient networkClient;
  PubSubClient mqttClient;

  static const uint8_t MAX_SUBSCRIPTIONS = 8;
  static const unsigned long UPTIME_PUBLISH_INTERVAL_MILLISECONDS = 5000;
  String subscribedTopics[MAX_SUBSCRIPTIONS];
  uint8_t subscribedTopicCount = 0;
};
