#include <Arduino.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"
#include "panel_input.h"

// Isolierter Test für das Control-Panel — kein Bluepad32, nur WiFi+MQTT und
// die GPIO-Buttons. Nutzt PanelInput unveraendert aus src_claw_player_input,
// damit hier keine Logik dupliziert wird. Sendet auf dasselbe Topic wie der
// echte Player-Input-Controller, damit sich das 1:1 als Ersatz testen laesst,
// solange dessen Bluepad32-Build nicht baut.

static constexpr const char *PANEL_TOPIC = "clawmachine/player_input/panel";

ClawMqttConnection mqttConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    "claw_panel_test",
    CLAW_MQTT_USER_USERNAME,
    CLAW_MQTT_USER_PASSWORD,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

PanelInput panelInput;
char payload_backup[192];

void printPanelState()
{
  Serial.print("[PANEL] up=");
  Serial.print(panelInput.up_button);
  Serial.print(" down=");
  Serial.print(panelInput.down_button);
  Serial.print(" left=");
  Serial.print(panelInput.left_button);
  Serial.print(" right=");
  Serial.print(panelInput.right_button);
  Serial.print(" front=");
  Serial.print(panelInput.front_button);
  Serial.print(" back=");
  Serial.print(panelInput.back_button);
  Serial.print(" grab=");
  Serial.print(panelInput.grab_button);
  Serial.print(" release=");
  Serial.print(panelInput.release_button);
  Serial.print(" valid=");
  Serial.println(panelInput.isValid());
}

void publishPanelState()
{
  if (!panelInput.isValid())
  {
    return;
  }



  char payload[192];
  snprintf(
      payload,
      sizeof(payload),
      "{\"up\":%d,\"down\":%d,\"left\":%d,\"right\":%d,\"front\":%d,\"back\":%d,\"grab\":%d,\"release\":%d}",
      panelInput.up_button,
      panelInput.down_button,
      panelInput.left_button,
      panelInput.right_button,
      panelInput.front_button,
      panelInput.back_button,
      panelInput.grab_button,
      panelInput.release_button);

if (strcmp(payload, payload_backup) != 0)
  {
  mqttConnection.publish(PANEL_TOPIC, payload);
  Serial.printf("[PANEL] Published: %s\n", payload);
  }
  else
  {
    return;
  }



  payload_backup[0] = '\0';
  strncpy(payload_backup, payload, sizeof(payload_backup) - 1);
  payload_backup[sizeof(payload_backup) - 1] = '\0';

}

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[PANEL] Panel-Test startet");

  mqttConnection.begin();
  panelInput.begin();
}

void loop()
{
  mqttConnection.maintainConnection();

  static unsigned long lastReadMs = 0;
  const unsigned long now = millis();

  if (now - lastReadMs >= 10)
  {
    lastReadMs = now;
    panelInput.read();
    printPanelState();
    publishPanelState();
  }
}
