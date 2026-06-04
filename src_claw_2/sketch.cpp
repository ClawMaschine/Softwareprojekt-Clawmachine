#include <Arduino.h>
#include <Bluepad32.h>

ControllerPtr connectedControllers[BP32_MAX_GAMEPADS];

static unsigned long lastPrintMs = 0;

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
                properties.btaddr[0],
                properties.btaddr[1],
                properties.btaddr[2],
                properties.btaddr[3],
                properties.btaddr[4],
                properties.btaddr[5]);

            Serial.printf(
                "[BLUEPAD32] VID: 0x%04x, PID: 0x%04x, flags: 0x%02x\n",
                properties.vendor_id,
                properties.product_id,
                properties.flags);

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

void printControllerState(ControllerPtr controller, int index)
{
    if (!controller->isConnected())
    {
        return;
    }

    if (!controller->hasData())
    {
        return;
    }

    Serial.print("[JOYCON ");
    Serial.print(index);
    Serial.print("] ");

    Serial.print("axisX=");
    Serial.print(controller->axisX());

    Serial.print(" axisY=");
    Serial.print(controller->axisY());

    Serial.print(" axisRX=");
    Serial.print(controller->axisRX());

    Serial.print(" axisRY=");
    Serial.print(controller->axisRY());

    Serial.print(" throttle=");
    Serial.print(controller->throttle());

    Serial.print(" brake=");
    Serial.print(controller->brake());

    Serial.print(" buttons=0x");
    Serial.print(controller->buttons(), HEX);

    Serial.print(" dpad=0x");
    Serial.print(controller->dpad(), HEX);

    Serial.print(" misc=0x");
    Serial.println(controller->miscButtons(), HEX);
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("[BOOT] ESP32 Bluepad32 Joy-Con test");
    Serial.println("[BOOT] Starting Bluepad32...");

    const uint8_t *address = BP32.localBdAddress();

    Serial.print("[BLUEPAD32] Local Bluetooth address: ");
    for (int i = 0; i < 6; i++)
    {
        if (i > 0)
        {
            Serial.print(":");
        }
        Serial.printf("%02X", address[i]);
    }
    Serial.println();

    BP32.setup(&onConnectedController, &onDisconnectedController);

    /*
      Nur beim ersten Test praktisch:
      Löscht alte Pairings auf dem ESP32.
      Wenn dein Joy-Con danach einmal sauber verbindet,
      diese Zeile auskommentieren, sonst muss er jedes Mal neu pairen.
    */
    BP32.forgetBluetoothKeys();

    /*
      true = virtuelle Geräte erlauben.
      Für normale Controller egal, aber für Tests unschädlich.
    */
    BP32.enableVirtualDevice(false);

    Serial.println("[BLUEPAD32] Ready.");
    Serial.println("[PAIRING] Put Joy-Con into pairing mode:");
    Serial.println("[PAIRING] Sync button am Joy-Con gedrueckt halten, bis LEDs laufen.");
}

void loop()
{
    bool dataUpdated = BP32.update();

    if (dataUpdated)
    {
        unsigned long now = millis();

        if (now - lastPrintMs >= 100)
        {
            lastPrintMs = now;

            for (int i = 0; i < BP32_MAX_GAMEPADS; i++)
            {
                if (connectedControllers[i] != nullptr)
                {
                    printControllerState(connectedControllers[i], i);
                }
            }
        }
    }

    delay(10);
}
