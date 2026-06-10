#include <Arduino.h>
#include <Bluepad32.h>

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

ClawMqttConnection playerInputConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_PLAYER_INPUT_CLIENT_ID,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

ControllerPtr connectedControllers[BP32_MAX_GAMEPADS];

static unsigned long lastControllerReadMs = 0;

void onConnectedController(ControllerPtr controller)
{
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    {
        if (connectedControllers[i] == nullptr)
        {
            connectedControllers[i] = controller;

            Serial.print("[BLUEPAD32] Controller connected at index ");
            Serial.println(i);

            ControllerProperties properties = controller->getProperties();
            Serial.printf(
                "[BLUEPAD32] BTAddr: %02x:%02x:%02x:%02x:%02x:%02x\n",
                properties.btaddr[0], properties.btaddr[1],
                properties.btaddr[2], properties.btaddr[3],
                properties.btaddr[4], properties.btaddr[5]);

            return;
        }
    }

    Serial.println("[BLUEPAD32] Controller connected, but no free slot available");
}

void onDisconnectedController(ControllerPtr controller)
{
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
    {
        if (connectedControllers[i] == controller)
        {
            connectedControllers[i] = nullptr;

            Serial.print("[BLUEPAD32] Controller disconnected from index ");
            Serial.println(i);
            return;
        }
    }

    Serial.println("[BLUEPAD32] Disconnected controller was not found");
}

void readControllerInput(ControllerPtr controller, int index)
{
    if (!controller->isConnected() || !controller->hasData())
    {
        return;
    }

    Serial.print("[JOYCON ");
    Serial.print(index);
    Serial.print("] ");
    Serial.print("axisX=");      Serial.print(controller->axisX());
    Serial.print(" axisY=");     Serial.print(controller->axisY());
    Serial.print(" axisRX=");    Serial.print(controller->axisRX());
    Serial.print(" axisRY=");    Serial.print(controller->axisRY());
    Serial.print(" buttons=0x"); Serial.print(controller->buttons(), HEX);
    Serial.print(" dpad=0x");    Serial.println(controller->dpad(), HEX);

    // TODO: Controller-Input per MQTT publizieren
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println("[PLAYER_INPUT] MQTT client starts");
    Serial.print("[PLAYER_INPUT] Client ID: ");
    Serial.println(CLAW_PLAYER_INPUT_CLIENT_ID);
    playerInputConnection.begin();

    Serial.println("[BLUEPAD32] Starting...");

    const uint8_t *address = BP32.localBdAddress();
    Serial.print("[BLUEPAD32] Local BT address: ");
    for (int i = 0; i < 6; i++)
    {
        if (i > 0) Serial.print(":");
        Serial.printf("%02X", address[i]);
    }
    Serial.println();

    BP32.setup(&onConnectedController, &onDisconnectedController);
    BP32.forgetBluetoothKeys();
    BP32.enableVirtualDevice(false);

    Serial.println("[BLUEPAD32] Ready. Put Joy-Con into pairing mode.");
}

void loop()
{
    playerInputConnection.maintainConnection();

    bool dataUpdated = BP32.update();
    if (dataUpdated)
    {
        unsigned long now = millis();
        if (now - lastControllerReadMs >= 100)
        {
            lastControllerReadMs = now;
            for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
            {
                if (connectedControllers[i] != nullptr)
                {
                    readControllerInput(connectedControllers[i], i);
                }
            }
        }
    }

    delay(10);
}
