#include "sc_wifi_configuration.h"
#include <cstdio>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cJSON.h>
#include "sc_ssid_manager.h"

#define TAG "WifiConfigurationSc"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define ESPTOUCH_DONE_BIT BIT2

WifiConfigurationSc &WifiConfigurationSc::GetInstance()
{
    static WifiConfigurationSc instance;
    return instance;
}

WifiConfigurationSc::WifiConfigurationSc()
{
    event_group_ = xEventGroupCreate();
    smartconfig_type_ = SC_TYPE_AIRKISS;
}

WifiConfigurationSc::~WifiConfigurationSc()
{
    if (event_group_)
    {
        vEventGroupDelete(event_group_);
    }
    // Unregister event handlers if they were registered
    if (instance_any_id_)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
    }
    if (instance_got_ip_)
    {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
    }
    if (sc_event_instance_)
    {
        esp_event_handler_instance_unregister(SC_EVENT, ESP_EVENT_ANY_ID, sc_event_instance_);
    }
}

void WifiConfigurationSc::SetSmartConfigType(const smartconfig_type_t type)
{
    smartconfig_type_ = type;
}

void WifiConfigurationSc::SetCallback(wifi_cfg_sc_cb_t callback)
{
    callback_ = callback;
}

void WifiConfigurationSc::Start()
{
    // Initialize the TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    // Create the default wifi sta
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    // Init WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiConfigurationSc::WifiEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiConfigurationSc::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(SC_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiConfigurationSc::SmartConfigEventHandler,
                                                        this,
                                                        &sc_event_instance_));

    // Start WiFi
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
}

std::string WifiConfigurationSc::GetSsid()
{
    // Get MAC and use it to generate a unique SSID
    uint8_t mac[6];
#if CONFIG_IDF_TARGET_ESP32P4
    esp_wifi_get_mac(WIFI_IF_AP, mac);
#else
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP));
#endif
    char ssid[32];
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", ssid_prefix_.c_str(), mac[4], mac[5]);
    return std::string(ssid);
}

std::string WifiConfigurationSc::GetWebServerUrl()
{
    return "https://s.c1ns.cn/LHBkP";
}

bool WifiConfigurationSc::ConnectToWifi(const std::string &ssid, const std::string &password)
{
    if (ssid.empty())
    {
        ESP_LOGE(TAG, "SSID cannot be empty");
        return false;
    }

    if (ssid.length() > 32)
    { // WiFi SSID 最大长度
        ESP_LOGE(TAG, "SSID too long");
        return false;
    }

    is_connecting_ = true;
    esp_wifi_scan_stop();
    xEventGroupClearBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    wifi_config_t wifi_config;
    bzero(&wifi_config, sizeof(wifi_config));
    strcpy((char *)wifi_config.sta.ssid, ssid.c_str());
    strcpy((char *)wifi_config.sta.password, password.c_str());
    wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_config.sta.failure_retry_cnt = 1;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    auto ret = esp_wifi_connect();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %d", ret);
        is_connecting_ = false;
        return false;
    }
    ESP_LOGI(TAG, "Connecting to WiFi %s", ssid.c_str());

    // Wait for the connection to complete for 5 seconds
    EventBits_t bits = xEventGroupWaitBits(event_group_, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    is_connecting_ = false;

    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connected to WiFi %s", ssid.c_str());
        esp_wifi_disconnect();
        return true;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to connect to WiFi %s", ssid.c_str());
        return false;
    }
}

void WifiConfigurationSc::Save(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "Save SSID %s %d", ssid.c_str(), ssid.length());
    SsidManager::GetInstance().AddSsid(ssid, password);
}

void WifiConfigurationSc::WifiEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);
    if (event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "WiFi event: STA started");
        xTaskCreate(smartconfig_task, "smartconfig_task", 4096, self, 3, NULL);
    }
    else if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
    }
    else if (event_id == WIFI_EVENT_SCAN_DONE)
    {
        std::lock_guard<std::mutex> lock(self->mutex_);
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);

        self->ap_records_.resize(ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, self->ap_records_.data());

        // 扫描完成，等待10秒后再次扫描
        esp_timer_start_once(self->scan_timer_, 10 * 1000000);
    }
}

void WifiConfigurationSc::IpEventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);
    if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
}

void WifiConfigurationSc::SmartConfigEventHandler(void *arg, esp_event_base_t event_base,
                                                  int32_t event_id, void *event_data)
{
    WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);

    if (event_base == SC_EVENT)
    {
        switch (event_id)
        {
        case SC_EVENT_SCAN_DONE:
            ESP_LOGI(TAG, "SmartConfig scan done");
            break;
        case SC_EVENT_FOUND_CHANNEL:
            ESP_LOGI(TAG, "Found SmartConfig channel");
            break;
        case SC_EVENT_GOT_SSID_PSWD:
        {
            ESP_LOGI(TAG, "Got SmartConfig credentials");
            smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
            wifi_config_t wifi_config;

            bzero(&wifi_config, sizeof(wifi_config_t));
            bzero(self->ssid, 33);
            bzero(self->password, 65);

            memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
            memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
            memcpy(self->ssid, evt->ssid, sizeof(evt->ssid));
            memcpy(self->password, evt->password, sizeof(evt->password));

            ESP_LOGI(TAG, "SmartConfig SSID: %s, Password: %s", self->ssid, self->password);

            ESP_ERROR_CHECK( esp_wifi_disconnect() );
            ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
            esp_wifi_connect();
            break;
        }
        case SC_EVENT_SEND_ACK_DONE:
            ESP_LOGI(TAG, "SmartConfig ACK sent");
            xEventGroupSetBits(self->event_group_, ESPTOUCH_DONE_BIT);
            break;
        }
    }
}

void WifiConfigurationSc::smartconfig_task(void *arg)
{
    WifiConfigurationSc *self = static_cast<WifiConfigurationSc *>(arg);
    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_AIRKISS) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        uxBits = xEventGroupWaitBits(self->event_group_, WIFI_CONNECTED_BIT | ESPTOUCH_DONE_BIT, true, false, portMAX_DELAY);
        if(uxBits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();

            self->Save(self->ssid, self->password);

            if (self->callback_ != nullptr)
            {
                self->callback_();
            }
            vTaskDelete(NULL);
        }
    }
}

void WifiConfigurationSc::Stop()
{
    // 停止SmartConfig服务
    if (sc_event_instance_)
    {
        esp_event_handler_instance_unregister(SC_EVENT, ESP_EVENT_ANY_ID, sc_event_instance_);
        sc_event_instance_ = nullptr;
    }
    esp_smartconfig_stop();

    // 停止定时器
    if (scan_timer_)
    {
        esp_timer_stop(scan_timer_);
        esp_timer_delete(scan_timer_);
        scan_timer_ = nullptr;
    }

    // 停止Web服务器
    if (server_)
    {
        httpd_stop(server_);
        server_ = nullptr;
    }

    // 停止DNS服务器
    dns_server_.Stop();

    // 注销事件处理器
    if (instance_any_id_)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }
    if (instance_got_ip_)
    {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }

    // 停止WiFi并重置模式
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_wifi_set_mode(WIFI_MODE_NULL);

    // 释放网络接口资源
    if (ap_netif_)
    {
        esp_netif_destroy(ap_netif_);
        ap_netif_ = nullptr;
    }

    ESP_LOGI(TAG, "Wifi configuration AP stopped");
}
