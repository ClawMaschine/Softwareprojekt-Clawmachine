#include "claw_mqtt_connection.h"

#include <ArduinoJson.h>


ClawMqttConnection::ClawMqttConnection(
    const char *wifiSsid,
    const char *wifiPassword,
    const char *mqttBrokerHost,
    uint16_t mqttBrokerPort,
    const char *mqttClientId,
    const char *mqttUsername,
    const char *mqttPassword,
    unsigned long reconnectIntervalMilliseconds)
    : wifiSsid(wifiSsid),
      wifiPassword(wifiPassword),
      mqttBrokerHost(mqttBrokerHost),
      mqttBrokerPort(mqttBrokerPort),
      mqttClientId(mqttClientId),
      mqttUsername(mqttUsername),
      mqttPassword(mqttPassword),
      reconnectIntervalMilliseconds(reconnectIntervalMilliseconds),
      lastWifiConnectAttemptMilliseconds(0),
      lastMqttConnectAttemptMilliseconds(0),
      mqttClient(networkClient) {}

void ClawMqttConnection::begin()
{
  WiFi.mode(WIFI_STA);
  mqttClient.setServer(mqttBrokerHost, mqttBrokerPort);
}

void ClawMqttConnection::maintainConnection()
{
  if (!ensureWifiConnected()) {
    return;
  }

  ensureMqttConnected();
  mqttClient.loop();

  String uptimeTopic = String("clawmachine/") + mqttClientId + "/metadata/uptime";
  mqttClient.publish(uptimeTopic.c_str(), String(millis()).c_str());
  
}

bool ClawMqttConnection::ensureWifiConnected()
{
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  const unsigned long now = millis();
  if (now - lastWifiConnectAttemptMilliseconds < reconnectIntervalMilliseconds) {
    return false;
  }

  lastWifiConnectAttemptMilliseconds = now;
  Serial.print("[MQTT_CLIENT] Connects to WiFi SSID: ");
  Serial.println(wifiSsid);
  WiFi.begin(wifiSsid, wifiPassword);

  const unsigned long connectStartedMilliseconds = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - connectStartedMilliseconds < 10000) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[MQTT_CLIENT] WiFi connection failed");
    return false;
  }

  Serial.print("[MQTT_CLIENT] WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool ClawMqttConnection::ensureMqttConnected()
{
  if (mqttClient.connected()) {
    return true;
  }

  const unsigned long now = millis();
  if (now - lastMqttConnectAttemptMilliseconds < reconnectIntervalMilliseconds) {
    return false;
  }

  lastMqttConnectAttemptMilliseconds = now;
  Serial.print("[MQTT_CLIENT] Connects to broker ");
  Serial.print(mqttBrokerHost);
  Serial.print(':');
  Serial.println(mqttBrokerPort);

  String statusTopic = String("clawmachine/") + mqttClientId + "/status";
  const bool connectionSuccessful = mqttClient.connect(
      mqttClientId, nullptr, nullptr,
      statusTopic.c_str(), 1, true, "offline");
  if (!connectionSuccessful) {
    Serial.print("[MQTT_CLIENT] Broker connection failed, rc=");
    Serial.println(mqttClient.state());
    return false;
  }

  Serial.print("[MQTT_CLIENT] MQTT connected as ");
  Serial.println(mqttClientId);
  mqttClient.publish(statusTopic.c_str(), "online", true);
  resubscribeAll();
  return true;
}

void ClawMqttConnection::setMessageCallback(void (*callback)(char *, uint8_t *, unsigned int))
{
  mqttClient.setCallback(callback);
}

void ClawMqttConnection::subscribe(const char *topic)
{
  if (subscribedTopicCount >= MAX_SUBSCRIPTIONS) {
    Serial.println("[MQTT_CLIENT] Max subscriptions reached");
    return;
  }
  subscribedTopics[subscribedTopicCount++] = String(topic);
  if (mqttClient.connected()) {
    mqttClient.subscribe(topic);
  }
}

void ClawMqttConnection::publish(const char *topic, const char *payload)
{
  if (!mqttClient.connected()) {
    return;
  }
  mqttClient.publish(topic, payload);
}

void ClawMqttConnection::resubscribeAll()
{
  for (uint8_t i = 0; i < subscribedTopicCount; i++) {
    mqttClient.subscribe(subscribedTopics[i].c_str());
    Serial.print("[MQTT_CLIENT] Resubscribed to ");
    Serial.println(subscribedTopics[i]);
  }
}
