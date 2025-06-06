#ifndef _WIFI_CONFIGURATION_AP_H_
#define _WIFI_CONFIGURATION_AP_H_

#include <string>
#include <vector>
#include <mutex>

#include <esp_http_server.h>
#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>

#include "sc_dns_server.h"
#include <esp_smartconfig.h>

typedef void (*wifi_cfg_sc_cb_t)();

class WifiConfigurationSc {
public:
    static WifiConfigurationSc& GetInstance();
    void SetSmartConfigType(const smartconfig_type_t type);
    void SetCallback(wifi_cfg_sc_cb_t callback);
    void Start();
    void Stop();

    std::string GetSsid();
    std::string GetWebServerUrl();

    // Delete copy constructor and assignment operator
    WifiConfigurationSc(const WifiConfigurationSc&) = delete;
    WifiConfigurationSc& operator=(const WifiConfigurationSc&) = delete;

private:
    wifi_cfg_sc_cb_t callback_ = nullptr; // 回调函数指针

    // Private constructor
    WifiConfigurationSc();
    ~WifiConfigurationSc();

    char ssid[33] = { 0 };
    char password[65] = { 0 };

    std::mutex mutex_;
    DnsServer dns_server_;
    httpd_handle_t server_ = NULL;
    EventGroupHandle_t event_group_;
    std::string ssid_prefix_;
    std::string language_;
    smartconfig_type_t smartconfig_type_;
    esp_event_handler_instance_t instance_any_id_ = nullptr;
    esp_event_handler_instance_t instance_got_ip_ = nullptr;
    esp_event_handler_instance_t sc_event_instance_ = nullptr;
    esp_timer_handle_t scan_timer_ = nullptr;
    bool is_connecting_ = false;
    esp_netif_t* ap_netif_ = nullptr;
    std::vector<wifi_ap_record_t> ap_records_;

    // 高级配置项
    std::string ota_url_;
    int8_t max_tx_power_;
    bool remember_bssid_;

    void StartAccessPoint();
    void StartWebServer();
    bool ConnectToWifi(const std::string &ssid, const std::string &password);
    void Save(const std::string &ssid, const std::string &password);

    // Event handlers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void SmartConfigEventHandler(void* arg, esp_event_base_t event_base, 
                                      int32_t event_id, void* event_data);

    static void smartconfig_task(void * parm);
};

#endif // _WIFI_CONFIGURATION_AP_H_
