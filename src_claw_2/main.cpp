#include <cstring>
#include <cstdio>

extern "C" {
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_hidh.h"
}

static const char *TAG = "JOYCON";

static void hidh_callback(void *handler_args,
                          esp_event_base_t base,
                          int32_t id,
                          void *event_data)
{
    auto *event = static_cast<esp_hidh_event_data_t *>(event_data);

    switch (id) {
    case ESP_HIDH_OPEN_EVENT:
        ESP_LOGI(TAG, "Joy-Con verbunden");
        break;

    case ESP_HIDH_INPUT_EVENT:
        ESP_LOGI(TAG, "Input Report empfangen, Länge: %d", event->input.length);

        for (int i = 0; i < event->input.length; i++) {
            printf("%02X ", event->input.data[i]);
        }
        printf("\n");
        break;

    case ESP_HIDH_CLOSE_EVENT:
        ESP_LOGI(TAG, "Joy-Con getrennt");
        break;

    default:
        break;
    }
}

static void scan_callback(esp_hid_scan_result_t *result)
{
    if (result == nullptr || result->name == nullptr) {
        return;
    }

    ESP_LOGI(TAG, "Gefunden: %s", result->name);

    if (strstr(result->name, "Joy-Con") != nullptr) {
        ESP_LOGI(TAG, "Joy-Con erkannt, verbinde...");

        esp_hidh_dev_open(
            result->bda,
            result->transport,
            result->ble.addr_type
        );
    }
}

extern "C" void app_main()
{
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_hidh_config_t config = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = nullptr
    };

    ESP_ERROR_CHECK(esp_hidh_init(&config));

    ESP_LOGI(TAG, "Scanne nach Joy-Con...");

    esp_hid_scan(
        10,
        sizeof(esp_hid_scan_result_t),
        scan_callback
    );
}