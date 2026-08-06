#include <Arduino.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "nvs_flash.h"

// ============================================================================
// EXPERIMENTELL — ungetestet auf echter Hardware.
//
// Nintendo-Switch-2-Controller (Joy-Con 2, Pro Controller 2, NSO-GameCube-
// Controller) sprechen kein Standard-BLE-HID (HOGP) und auch kein normales
// SMP-Pairing, sondern ein komplett proprietäres GATT-Protokoll mit eigenem
// Pairing-Schema. Bluepad32 unterstützt sie (Stand jetzt) nicht.
//
// Diese Firmware basiert auf der Reverse-Engineering-Dokumentation von
// ndeadly: https://github.com/ndeadly/switch2_controller_research
//
// Vereinfachungen gegenüber dem offiziellen Verbindungsaufbau:
// - Kein offizielles Pairing (der AES-LTK-Handshake aus der Doku wird nicht
//   durchgeführt) — laut Doku ist Pairing nur nötig, damit sich der
//   Controller automatisch wieder mit der Switch verbindet bzw. sie aus dem
//   Standby weckt. Für reines Auslesen von Eingaben reicht eine offene
//   (unverschlüsselte) BLE-Verbindung.
// - Es wird nur "Input Report 0x05" ausgelesen (Tasten + beide Analogsticks),
//   weil dieser Report bei ALLEN Controllertypen unter denselben GATT-Handles
//   erreichbar ist — kein Bedarf, Joy-Con/Pro/GameCube zu unterscheiden.
// - Keine Bonding-Persistenz: nach einem Neustart muss neu verbunden werden.
//
// Unklar/nicht verifiziert: ob der Controller Notifications auf diesem
// Report auch ohne die von der Doku dokumentierte Init-Befehlssequenz
// (Feature-Flags etc. auf Handle 0x0016) tatsächlich sendet. Falls hier
// nichts ankommt, ist das der erste Verdächtige.
// ============================================================================

static const uint16_t NINTENDO_MANUFACTURER_ID = 0x0553;
static const uint16_t NINTENDO_VENDOR_ID       = 0x057E;
static const uint16_t SWITCH2_PRODUCT_ID_MIN   = 0x2060;

// Input Report 0x05 (siehe hid_reports.md): Service ab7de9be-...-fd0,
// Characteristic-Handle 0x000A, zugehöriger CCCD-Handle 0x000B.
static const uint16_t INPUT_REPORT_HANDLE      = 0x000A;
static const uint16_t INPUT_REPORT_CCCD_HANDLE = 0x000B;

static esp_gatt_if_t     gattClientInterface = ESP_GATT_IF_NONE;
static uint16_t          activeConnId        = 0;
static bool              controllerFound     = false;
static esp_bd_addr_t     controllerAddress   = {0};
static esp_ble_addr_type_t controllerAddressType = BLE_ADDR_TYPE_PUBLIC;

static esp_ble_scan_params_t bleScanParameters = {
    .scan_type = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval = 0x50,
    .scan_window = 0x30,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
};

// --- Werbepaket-Erkennung ---------------------------------------------------
//
// Layout der Manufacturer-Specific-Data (siehe bluetooth_interface.md):
// 0x0-0x1 Manufacturer-ID (immer 0x0553/Nintendo), 0x5-0x6 Vendor-ID
// (0x057E), 0x7-0x8 Produkt-ID (Switch 2 ab 0x2060), 0xC-0x11 Host-Adresse
// (nur bei "Standard"-Advertisement, also im Pairing-Modus, komplett 0x00 —
// bei "Reconnection"/"Wake" steht dort die Adresse eines bereits gekoppelten
// Hosts, den wollen wir hier nicht stören).

static bool isSwitch2ControllerAdvertisement(uint8_t *advData, uint8_t advDataLength)
{
  uint8_t manufacturerDataLength = 0;
  uint8_t *manufacturerData = esp_ble_resolve_adv_data(
      advData,
      ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE,
      &manufacturerDataLength);

  if (manufacturerData == nullptr || manufacturerDataLength < 0x13)
  {
    return false;
  }

  uint16_t manufacturerId = manufacturerData[0] | (manufacturerData[1] << 8);
  uint16_t vendorId       = manufacturerData[5] | (manufacturerData[6] << 8);
  uint16_t productId      = manufacturerData[7] | (manufacturerData[8] << 8);

  if (manufacturerId != NINTENDO_MANUFACTURER_ID ||
      vendorId != NINTENDO_VENDOR_ID ||
      productId < SWITCH2_PRODUCT_ID_MIN)
  {
    return false;
  }

  for (int i = 0xC; i <= 0x11; i++)
  {
    if (manufacturerData[i] != 0x00)
    {
      return false; // Reconnection/Wake-Advertisement fuer einen anderen Host
    }
  }

  return true;
}

// --- Input Report 0x05 parsen ----------------------------------------------
//
// Siehe hid_reports.md#input-report-0x05. Sticks sind als gepackte 12-Bit-
// Werte in je 3 Bytes kodiert (dasselbe Schema wie bei den originalen
// Joy-Cons).

struct StickValues
{
  uint16_t x;
  uint16_t y;
};

static StickValues unpackStick(const uint8_t *bytes)
{
  StickValues stick;
  stick.x = bytes[0] | ((bytes[1] & 0x0F) << 8);
  stick.y = (bytes[1] >> 4) | (bytes[2] << 4);
  return stick;
}

static void printInputReport05(const uint8_t *data, uint16_t length)
{
  if (length < 0x10)
  {
    Serial.printf("[SWITCH2] Report zu kurz (%u Bytes)\n", length);
    return;
  }

  uint32_t buttons =
      data[0x4] | (data[0x5] << 8) | (data[0x6] << 16) | (static_cast<uint32_t>(data[0x7]) << 24);

  StickValues leftStick  = unpackStick(&data[0xA]);
  StickValues rightStick = unpackStick(&data[0xD]);

  Serial.printf(
      "[SWITCH2] buttons=0x%08lX leftStick=(%u,%u) rightStick=(%u,%u)\n",
      static_cast<unsigned long>(buttons),
      leftStick.x, leftStick.y,
      rightStick.x, rightStick.y);
}

// --- GAP (Scannen + Verbindungsaufbau) --------------------------------------

static void onBleGapEvent(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
  if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT)
  {
    esp_ble_gap_start_scanning(30);
    return;
  }

  if (event != ESP_GAP_BLE_SCAN_RESULT_EVT)
  {
    return;
  }

  if (param->scan_rst.search_evt == ESP_GAP_SEARCH_INQ_CMPL_EVT)
  {
    if (!controllerFound)
    {
      esp_ble_gap_start_scanning(30); // Nichts gefunden -> weiterscannen
    }
    return;
  }

  if (param->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT || controllerFound)
  {
    return;
  }

  if (!isSwitch2ControllerAdvertisement(param->scan_rst.ble_adv, param->scan_rst.adv_data_len))
  {
    return;
  }

  controllerFound = true;
  memcpy(controllerAddress, param->scan_rst.bda, sizeof(esp_bd_addr_t));
  controllerAddressType = param->scan_rst.ble_addr_type;

  Serial.print("[SWITCH2] Controller gefunden: ");
  for (int i = 0; i < 6; i++)
  {
    Serial.printf("%02X%s", controllerAddress[i], i < 5 ? ":" : "\n");
  }

  esp_ble_gap_stop_scanning();
  esp_ble_gattc_open(gattClientInterface, controllerAddress, controllerAddressType, true);
}

// --- GATT-Client -------------------------------------------------------------

static void onGattcEvent(esp_gattc_cb_event_t event, esp_gatt_if_t gattcInterface, esp_ble_gattc_cb_param_t *param)
{
  switch (event)
  {
    case ESP_GATTC_REG_EVT:
      gattClientInterface = gattcInterface;
      break;

    case ESP_GATTC_CONNECT_EVT:
      activeConnId = param->connect.conn_id;
      Serial.println("[SWITCH2] BLE-Verbindung aufgebaut...");
      break;

    case ESP_GATTC_OPEN_EVT:
      if (param->open.status != ESP_GATT_OK)
      {
        Serial.printf("[SWITCH2] Verbindung fehlgeschlagen: 0x%02x\n", param->open.status);
        controllerFound = false;
        esp_ble_gap_start_scanning(30);
        return;
      }

      Serial.println("[SWITCH2] Verbunden. Aktiviere Input-Report-Notifications...");
      // Lokale Registrierung, damit ESP_GATTC_NOTIFY_EVT ueberhaupt ausgeloest wird.
      esp_ble_gattc_register_for_notify(gattcInterface, controllerAddress, INPUT_REPORT_HANDLE);
      break;

    case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    {
      if (param->reg_for_notify.status != ESP_GATT_OK)
      {
        Serial.printf("[SWITCH2] Notify-Registrierung fehlgeschlagen: 0x%02x\n", param->reg_for_notify.status);
        break;
      }

      // Ueber-die-Luft: dem Controller per CCCD-Schreibzugriff mitteilen,
      // dass er ab jetzt Notifications senden soll (Wert 0x0001).
      uint16_t notifyEnable = 0x0001;
      esp_ble_gattc_write_char_descr(
          gattcInterface,
          activeConnId,
          INPUT_REPORT_CCCD_HANDLE,
          sizeof(notifyEnable),
          reinterpret_cast<uint8_t *>(&notifyEnable),
          ESP_GATT_WRITE_TYPE_RSP,
          ESP_GATT_AUTH_REQ_NONE);
      break;
    }

    case ESP_GATTC_WRITE_DESCR_EVT:
      if (param->write.status == ESP_GATT_OK)
      {
        Serial.println("[SWITCH2] Notifications aktiviert, warte auf Eingaben...");
      }
      else
      {
        Serial.printf("[SWITCH2] CCCD-Schreibzugriff fehlgeschlagen: 0x%02x\n", param->write.status);
      }
      break;

    case ESP_GATTC_NOTIFY_EVT:
      if (param->notify.handle == INPUT_REPORT_HANDLE)
      {
        printInputReport05(param->notify.value, param->notify.value_len);
      }
      break;

    case ESP_GATTC_DISCONNECT_EVT:
      Serial.println("[SWITCH2] Verbindung getrennt, scanne erneut...");
      controllerFound = false;
      esp_ble_gap_start_scanning(30);
      break;

    default:
      break;
  }
}

// --- Setup / Loop ------------------------------------------------------------

void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("[SWITCH2] Starte experimentellen Switch-2-Controller-Scanner");

  nvs_flash_init();

  // Nach einem Soft-Reset (z.B. durch den Monitor/EN-Pin beim Flashen, nicht
  // durch echtes Stromloswerden) kann der BT-Controller noch vom vorherigen
  // Lauf aktiv sein — dann fuehrt esp_bt_controller_init() unten teils sogar
  // zu einem Absturz/Boot-Loop statt nur zu ESP_ERR_INVALID_STATE. Deshalb
  // erst sauber deinitialisieren, falls er nicht im IDLE-Zustand ist.
  esp_bt_controller_status_t controllerStatus = esp_bt_controller_get_status();
  if (controllerStatus != ESP_BT_CONTROLLER_STATUS_IDLE)
  {
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
  }

  esp_bt_controller_config_t controllerConfiguration = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_bt_controller_init(&controllerConfiguration);
  esp_bt_controller_enable(ESP_BT_MODE_BLE);

  esp_bluedroid_init();
  esp_bluedroid_enable();

  esp_ble_gap_register_callback(onBleGapEvent);
  esp_ble_gattc_register_callback(onGattcEvent);
  esp_ble_gattc_app_register(0);

  esp_ble_gap_set_scan_params(&bleScanParameters);

  Serial.println("[SWITCH2] Scanne... Controller per Sync-Taste in Pairing-Modus bringen.");
}

void loop()
{
  delay(1000);
}
