#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include <nvs_flash.h>

#include "esp_wifi.h"
#include "esp_event.h"

#include <sc_wifi_station.h>
#include <sc_wifi_configuration.h>
#include <sc_ssid_manager.h>

#define TAG "AirKiss"

extern "C" void app_main(void)
{
    // Initialize the default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize NVS flash for Wi-Fi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Get the Wi-Fi configuration
    auto &ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty())
    {
        // Start the Wi-Fi configuration AP
        auto &ap = WifiConfigurationSc::GetInstance();
        ap.SetSmartConfigType(SC_TYPE_AIRKISS);
        ap.Start();
        return;
    }

    // Otherwise, connect to the Wi-Fi network
    WifiStation::GetInstance().Start();
}
