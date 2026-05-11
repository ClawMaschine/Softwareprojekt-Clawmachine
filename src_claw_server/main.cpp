#include <Arduino.h>
#include <WiFi.h>

static const char *kApSsid = "clawmachine_debug";
static const char *kApPassword = "";
static const uint16_t kServerPort = 3333;

WiFiServer Server(kServerPort);
WiFiClient client;
bool gClientRegistered = false;
String gRegisteredClientName;
unsigned long gLastPeriodicMessageMs = 0;

void sendToClient(const String &message) {
  if (!client.connected()) {
    return;
  }

  client.println(message);
  Serial.print("[SERVER->CLIENT] ");
  Serial.println(message);
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(kApSsid, kApPassword);
  Server.begin();
  Server.setNoDelay(true);

  Serial.println();
  Serial.println("[SERVER] WLAN AP gestartet");
  Serial.print("[SERVER] SSID: ");
  Serial.println(kApSsid);
  Serial.print("[SERVER] AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("[SERVER] TCP Port: ");
  Serial.println(kServerPort);
  Serial.println("[SERVER] Warte auf Client-Verbindung...");
}

void loop() {
  if (!client.connected()) {
    WiFiClient newClient = Server.available();
    if (newClient) {
      client = newClient;
      client.setTimeout(20);
      gClientRegistered = false;
      gRegisteredClientName = "";
      Serial.println("[SERVER] Client verbunden");
    }
  }

  if (client.connected() && client.available()) {
    String message = client.readStringUntil('\n');
    message.trim();

    if (message.length() > 0) {
      if (!gClientRegistered) {
        gClientRegistered = true;
        gRegisteredClientName = message;
        Serial.print("[SERVER] Client angemeldet als: ");
        Serial.println(gRegisteredClientName);
        sendToClient(String("Willkommen ") + gRegisteredClientName);
      } else {
        Serial.print("[CLIENT->SERVER] ");
        Serial.println(message);
      }
    }
  }

  if (!client.connected()) {
    if (gClientRegistered) {
      Serial.println("[SERVER] Client getrennt");
    }
    gClientRegistered = false;
    gRegisteredClientName = "";
  }

  if (client.connected() && gClientRegistered && Serial.available()) {
    String message = Serial.readStringUntil('\n');
    message.trim();
    if (message.length() > 0) {
      sendToClient(message);
    }
  }

  const unsigned long now = millis();
  if (client.connected() && gClientRegistered && now - gLastPeriodicMessageMs >= 5000) {
    gLastPeriodicMessageMs = now;
    const String heartbeat = String("Heartbeat ") + String(now / 1000) + "s";
    sendToClient(heartbeat);
  }

  delay(20);
}
