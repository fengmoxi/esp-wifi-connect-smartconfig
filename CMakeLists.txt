idf_component_register(
    SRCS
        "sc_wifi_configuration.cc"
        "sc_wifi_station.cc"
        "sc_ssid_manager.cc"
        "sc_dns_server.cc"
    INCLUDE_DIRS
        "include"
    REQUIRES
    	"esp_timer"
        "esp_http_server"
        "esp_wifi"
        "nvs_flash"
        "json"
)
