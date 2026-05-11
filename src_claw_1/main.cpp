#include <Arduino.h>
#include <WiFi.h>

static const char *kClientName = "claw_1";
static const char *kApSsid = "clawmachine_debug";
static const char *kApPassword = "";
static const IPAddress kServerIp(192, 168, 4, 1);
static const uint16_t kServerPort = 3333;
static const unsigned long kReconnectIntervalMs = 2000;

WiFiClient gClient;
bool gConnected = false;
unsigned long gLastConnectAttemptMs = 0;

bool ensureWifiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.print("[CLIENT] Verbinde mit AP ");
  Serial.println(kApSsid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(kApSsid, kApPassword);

  const unsigned long startMs = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMs < 10000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[CLIENT] AP-Verbindung fehlgeschlagen");
    return false;
  }

  Serial.print("[CLIENT] WLAN verbunden, IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool connectToServer() {
  if (!ensureWifiConnected()) {
    return false;
  }

  Serial.print("[CLIENT] Verbinde mit Server ");
  Serial.print(kServerIp);
  Serial.print(':');
  Serial.println(kServerPort);
  if (!gClient.connect(kServerIp, kServerPort)) {
    Serial.println("[CLIENT] Server-Verbindung fehlgeschlagen");
    return false;
  }

  gClient.setTimeout(20);
  gClient.println(kClientName);
  Serial.println("[CLIENT] Anmeldung an Server gesendet");
  Serial.println("[CLIENT] Verbunden und angemeldet");
  gConnected = true;
  return true;
}

void readIncomingMessages() {
  while (gClient.connected() && gClient.available()) {
    String message = gClient.readStringUntil('\n');
    message.trim();
    if (message.length() > 0) {
      Serial.print("[SERVER->CLIENT] ");
      Serial.println(message);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Serial.println();
  Serial.println("[CLIENT] WLAN Client gestartet");
  Serial.print("[CLIENT] Name: ");
  Serial.println(kClientName);
}

void loop() {
  if (gConnected && !gClient.connected()) {
    Serial.println("[CLIENT] Verbindung verloren");
    gClient.stop();
    gConnected = false;
  }

  if (!gConnected) {
    const unsigned long now = millis();
    if (now - gLastConnectAttemptMs >= kReconnectIntervalMs) {
      gLastConnectAttemptMs = now;
      connectToServer();
    }
  }

  if (gConnected) {
    readIncomingMessages();
  }

  delay(20);
}
