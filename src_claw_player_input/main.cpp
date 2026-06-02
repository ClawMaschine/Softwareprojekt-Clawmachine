#include <Arduino.h>
#include "BluetoothSerial.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"

#include "claw_mqtt_connection.h"
#include "firmware_config.h"

BluetoothSerial SerialBT;

ClawMqttConnection playerInputConnection(
    CLAW_CLIENT_WIFI_SSID,
    CLAW_CLIENT_WIFI_PASSWORD,
    CLAW_MQTT_BROKER_HOST,
    CLAW_MQTT_BROKER_PORT,
    CLAW_PLAYER_INPUT_CLIENT_ID,
    CLAW_CONNECTION_RETRY_INTERVAL_MS);

void btAdvertisedDeviceFound(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
  if (event == ESP_BT_GAP_DISC_RES_EVT)
  {
    Serial.println("----- Classic Bluetooth Gerät gefunden -----");

    char bda_str[18];
    snprintf(bda_str, sizeof(bda_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             param->disc_res.bda[0], param->disc_res.bda[1],
             param->disc_res.bda[2], param->disc_res.bda[3],
             param->disc_res.bda[4], param->disc_res.bda[5]);

    Serial.print("MAC: ");
    Serial.println(bda_str);

    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
      esp_bt_gap_dev_prop_t prop = param->disc_res.prop[i];

      if (prop.type == ESP_BT_GAP_DEV_PROP_EIR)
      {
        uint8_t len = 0;
        uint8_t *name = esp_bt_gap_resolve_eir_data(
            (uint8_t *)prop.val,
            ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME,
            &len);

        if (name)
        {
          Serial.print("Name: ");
          Serial.write(name, len);
          Serial.println();
        }
      }

      if (prop.type == ESP_BT_GAP_DEV_PROP_RSSI)
      {
        Serial.print("RSSI: ");
        Serial.println(*(int8_t *)prop.val);
      }
    }

    Serial.println("--------------------------------------------");
  }
  else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT)
  {
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED)
    {
      Serial.println("Scan fertig.");
      Serial.println("Starte neuen Scan...");
      esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("[PLAYER_INPUT] MQTT client starts");
  Serial.print("[PLAYER_INPUT] Client ID: ");
  Serial.println(CLAW_PLAYER_INPUT_CLIENT_ID);
  playerInputConnection.begin();

  Serial.println("Classic Bluetooth Scanner startet...");

  if (!SerialBT.begin("ESP32_BT_SCANNER"))
  {
    Serial.println("Bluetooth Start fehlgeschlagen!");
    return;
  }

  esp_bt_gap_register_callback(btAdvertisedDeviceFound);

  Serial.println("Starte Classic Bluetooth Inquiry...");
  esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
}

void loop()
{
  playerInputConnection.maintainConnection();
  delay(1000);
}
