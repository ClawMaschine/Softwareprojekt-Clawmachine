#include <Arduino.h>

#include "esp_bt.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "nvs_flash.h"
#include <algorithm>

static constexpr size_t MAX_BLUETOOTH_DEVICES = 20;

struct BluetoothDevice
{
    bool used;
    esp_bd_addr_t address;
    char name[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    int8_t rssi;
    uint32_t classOfDevice;
    unsigned long lastSeenMs;
};

BluetoothDevice bluetoothDevices[MAX_BLUETOOTH_DEVICES];

static void formatBluetoothAddress(
    const esp_bd_addr_t address,
    char *output,
    size_t outputSize)
{
    snprintf(
        output,
        outputSize,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        address[0],
        address[1],
        address[2],
        address[3],
        address[4],
        address[5]);
}

static int findBluetoothDevice(const esp_bd_addr_t address)
{
    for (size_t i = 0; i < MAX_BLUETOOTH_DEVICES; i++)
    {
        if (!bluetoothDevices[i].used)
        {
            continue;
        }

        if (memcmp(
                bluetoothDevices[i].address,
                address,
                ESP_BD_ADDR_LEN) == 0)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static int findFreeBluetoothDeviceSlot()
{
    for (size_t i = 0; i < MAX_BLUETOOTH_DEVICES; i++)
    {
        if (!bluetoothDevices[i].used)
        {
            return static_cast<int>(i);
        }
    }

    return -1;
}

static void handleDiscoveryResult(
    esp_bt_gap_cb_param_t::disc_res_param &result)
{
    int deviceIndex = findBluetoothDevice(result.bda);

    if (deviceIndex < 0)
    {
        deviceIndex = findFreeBluetoothDeviceSlot();
    }

    if (deviceIndex < 0)
    {
        Serial.println("[BT_SCAN] Device list full");
        return;
    }

    BluetoothDevice &device = bluetoothDevices[deviceIndex];

    if (!device.used)
    {
        memset(&device, 0, sizeof(device));
        device.used = true;
        memcpy(device.address, result.bda, ESP_BD_ADDR_LEN);
        strcpy(device.name, "<unknown>");
    }

    device.lastSeenMs = millis();

    for (int propertyIndex = 0;
         propertyIndex < result.num_prop;
         propertyIndex++)
    {
        const esp_bt_gap_dev_prop_t &property =
            result.prop[propertyIndex];

        switch (property.type)
        {
            case ESP_BT_GAP_DEV_PROP_BDNAME:
            {
                const size_t propertyLength =
                static_cast<size_t>(property.len);

                const size_t maximumNameLength =
                sizeof(device.name) - 1;

                const size_t nameLength =
                propertyLength < maximumNameLength
                ? propertyLength
                : maximumNameLength;

                memcpy(device.name, property.val, nameLength);
                device.name[nameLength] = '\0';
                break;
            }

            case ESP_BT_GAP_DEV_PROP_RSSI:
                device.rssi =
                    *static_cast<int8_t *>(property.val);
                break;

            case ESP_BT_GAP_DEV_PROP_COD:
                device.classOfDevice =
                    *static_cast<uint32_t *>(property.val);
                break;

            default:
                break;
        }
    }

    char addressText[18];
    formatBluetoothAddress(
        device.address,
        addressText,
        sizeof(addressText));

    Serial.printf(
        "[BT_SCAN] Found address=%s name=\"%s\" "
        "rssi=%d cod=0x%06lX\n",
        addressText,
        device.name,
        device.rssi,
        static_cast<unsigned long>(device.classOfDevice));
}

static void bluetoothGapCallback(
    esp_bt_gap_cb_event_t event,
    esp_bt_gap_cb_param_t *parameter)
{
    switch (event)
    {
        case ESP_BT_GAP_DISC_RES_EVT:
            handleDiscoveryResult(parameter->disc_res);
            break;

        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
            if (parameter->disc_st_chg.state ==
                ESP_BT_GAP_DISCOVERY_STARTED)
            {
                Serial.println("[BT_SCAN] Discovery started");
            }
            else if (
                parameter->disc_st_chg.state ==
                ESP_BT_GAP_DISCOVERY_STOPPED)
            {
                Serial.println(
                    "[BT_SCAN] Discovery stopped, restarting");

                esp_bt_gap_start_discovery(
                    ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                    10,
                    0);
            }
            break;

        default:
            break;
    }
}

static bool initializeBluetoothScanner()
{
    esp_err_t result = nvs_flash_init();

    if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
        result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] NVS initialization failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    // BLE-Speicher freigeben, weil hier nur Bluetooth Classic
    // verwendet wird.
    result = esp_bt_controller_mem_release(
        ESP_BT_MODE_BLE);

    if (result != ESP_OK &&
        result != ESP_ERR_INVALID_STATE)
    {
        Serial.printf(
            "[BT_SCAN] BLE memory release failed: %s\n",
            esp_err_to_name(result));
    }

    esp_bt_controller_config_t controllerConfiguration =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    result = esp_bt_controller_init(
        &controllerConfiguration);

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Controller initialization failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bt_controller_enable(
        ESP_BT_MODE_CLASSIC_BT);

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Controller enable failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bluedroid_init();

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Bluedroid initialization failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bluedroid_enable();

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Bluedroid enable failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bt_gap_register_callback(
        bluetoothGapCallback);

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] GAP callback registration failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bt_gap_set_scan_mode(
        ESP_BT_CONNECTABLE,
        ESP_BT_NON_DISCOVERABLE);

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Scan-mode configuration failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    result = esp_bt_gap_start_discovery(
        ESP_BT_INQ_MODE_GENERAL_INQUIRY,
        10,
        0);

    if (result != ESP_OK)
    {
        Serial.printf(
            "[BT_SCAN] Discovery start failed: %s\n",
            esp_err_to_name(result));
        return false;
    }

    return true;
}

static void printBluetoothDeviceList()
{
    Serial.println();
    Serial.println("[BT_SCAN] ===== Device list =====");

    size_t deviceCount = 0;

    for (size_t i = 0; i < MAX_BLUETOOTH_DEVICES; i++)
    {
        const BluetoothDevice &device =
            bluetoothDevices[i];

        if (!device.used)
        {
            continue;
        }

        char addressText[18];

        formatBluetoothAddress(
            device.address,
            addressText,
            sizeof(addressText));

        Serial.printf(
            "[BT_SCAN] %u: address=%s name=\"%s\" "
            "rssi=%d cod=0x%06lX age=%lums\n",
            static_cast<unsigned int>(deviceCount),
            addressText,
            device.name,
            device.rssi,
            static_cast<unsigned long>(
                device.classOfDevice),
            millis() - device.lastSeenMs);

        deviceCount++;
    }

    if (deviceCount == 0)
    {
        Serial.println("[BT_SCAN] No devices found");
    }

    Serial.println("[BT_SCAN] =======================");
}

void setup()
{
    Serial.begin(115200);
    delay(500);

    memset(
        bluetoothDevices,
        0,
        sizeof(bluetoothDevices));

    Serial.println("[BT_SCAN] Starting Bluetooth Classic scanner");

    if (!initializeBluetoothScanner())
    {
        Serial.println("[BT_SCAN] Initialization failed");
        return;
    }

    Serial.println("[BT_SCAN] Scanner ready");
}

void loop()
{
    static unsigned long lastDeviceListMs = 0;

    const unsigned long now = millis();

    if (now - lastDeviceListMs >= 1000)
    {
        lastDeviceListMs = now;
        printBluetoothDeviceList();
    }

    delay(10);
}