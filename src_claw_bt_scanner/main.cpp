#include <Arduino.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_gap_ble_api.h"
#include "nvs_flash.h"

// Einfacher Bluetooth-Scanner: sucht dauerhaft nach Classic- UND
// BLE-Geräten in der Umgebung und druckt jeden Fund sofort über Serial
// aus. Keine Geräteliste, keine Zustandsprüfung — für mehr Robustheit
// siehe src_claw_bluetooth_devices_test.

static const uint32_t BLE_SCAN_DURATION_SECONDS = 30;

static void printAddress(const esp_bd_addr_t address)
{
  Serial.printf(
      "%02X:%02X:%02X:%02X:%02X:%02X",
      address[0], address[1], address[2],
      address[3], address[4], address[5]);
}

// --- Bluetooth Classic ------------------------------------------------

static void onClassicGapEvent(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
  if (event == ESP_BT_GAP_DISC_RES_EVT)
  {
    const char *name = "<unknown>";

    for (int i = 0; i < param->disc_res.num_prop; i++)
    {
      if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_BDNAME)
      {
        name = static_cast<const char *>(param->disc_res.prop[i].val);
      }
    }

    Serial.print("[BT_SCAN] Classic gefunden: ");
    printAddress(param->disc_res.bda);
    Serial.print(" name=");
    Serial.println(name);
    return;
  }

  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT)
  {
    if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED)
    {
      Serial.println("[BT_SCAN] Classic-Discovery aktiv");
      return;
    }

    // Ein Inquiry-Lauf dauert nur begrenzt lange (siehe Timeout unten) —
    // danach direkt neu starten, um dauerhaft zu scannen.
    Serial.println("[BT_SCAN] Classic-Discovery beendet, starte neu");
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
  }
}

// --- Bluetooth Low Energy ----------------------------------------------

static esp_ble_scan_params_t bleScanParameters = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

static void onBleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)
  {
    Serial.printf("[BT_SCAN] BLE-Scan-Params gesetzt, status=%d\n", param->scan_param_cmpl.status);
    esp_ble_gap_start_scanning(BLE_SCAN_DURATION_SECONDS);
    return;
  }

  if (event == ESP_GAP_BLE_SCAN_START_COMPLETE_EVT)
  {
    // Bestaetigung, dass der Scan wirklich aktiv ist (esp_ble_gap_start_scanning
    // ist async — ohne dieses Event wüssten wir nicht, ob der Start klappte).
    Serial.printf("[BT_SCAN] BLE-Scan-Start, status=%d\n", param->scan_start_cmpl.status);
    return;
  }

  if (event != ESP_GAP_BLE_SCAN_RESULT_EVT)
  {
    return;
  }

  if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_RES_EVT)
  {
    Serial.print("[BT_SCAN] BLE gefunden: ");
    printAddress(param->scan_rst.bda);
    Serial.printf(" rssi=%d\n", param->scan_rst.rssi);
    return;
  }

  if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
  {
    // Scan-Durchlauf zu Ende — direkt neu starten, um dauerhaft zu scannen.
    Serial.println("[BT_SCAN] BLE-Scan-Zyklus beendet, starte neu");
    esp_ble_gap_start_scanning(BLE_SCAN_DURATION_SECONDS);
  }
}

// --- Setup / Loop --------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[BT_SCAN] Starte Bluetooth-Scanner (Classic + BLE)");

  nvs_flash_init();

  // Nach einem Soft-Reset (z.B. durch den Monitor/EN-Pin beim Flashen, nicht
  // durch echtes Stromloswerden) kann der BT-Controller noch vom vorherigen
  // Lauf aktiv sein — dann schlagen die Init-Aufrufe unten fehl, ohne dass
  // man das ohne Fehlerprüfung merkt. Deshalb erst sauber deinitialisieren,
  // falls er nicht im IDLE-Zustand ist.
  esp_bt_controller_status_t controllerStatus = esp_bt_controller_get_status();
  if (controllerStatus != ESP_BT_CONTROLLER_STATUS_IDLE)
  {
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
  }

  esp_bt_controller_config_t controllerConfiguration = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_err_t result = esp_bt_controller_init(&controllerConfiguration);
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] Controller-Init fehlgeschlagen: %s\n", esp_err_to_name(result));
    return;
  }

  result = esp_bt_controller_enable(ESP_BT_MODE_BTDM); // Dual-Mode: Classic + BLE gleichzeitig
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] Controller-Enable fehlgeschlagen: %s\n", esp_err_to_name(result));
    return;
  }

  result = esp_bluedroid_init();
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] Bluedroid-Init fehlgeschlagen: %s\n", esp_err_to_name(result));
    return;
  }

  result = esp_bluedroid_enable();
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] Bluedroid-Enable fehlgeschlagen: %s\n", esp_err_to_name(result));
    return;
  }

  esp_bt_gap_register_callback(onClassicGapEvent);
  esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
  result = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] Classic-Discovery-Start fehlgeschlagen: %s\n", esp_err_to_name(result));
  }

  esp_ble_gap_register_callback(onBleGapEvent);
  result = esp_ble_gap_set_scan_params(&bleScanParameters);
  if (result != ESP_OK)
  {
    Serial.printf("[BT_SCAN] BLE-Scan-Params fehlgeschlagen: %s\n", esp_err_to_name(result));
  }

  Serial.println("[BT_SCAN] Scanner läuft");
}

void loop()
{
  delay(1000);
}
