#include <cstring>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "board_config.h"
#include "wifi_manager.h"

extern "C" {
#include "mmhalow.h"
}

static const char *TAG = "wifi_manager";
static bool mdns_started = false;

// Runs automatically once the HaLow router assigns an IP via DHCP
static void on_got_ip(void *arg, esp_event_base_t event_base,
                      int32_t event_id, void *event_data)
{
    auto *event = static_cast<ip_event_got_ip_t *>(event_data);
    ESP_LOGI(TAG, "HaLow connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));

    if (!mdns_started) {
        ESP_LOGI(TAG, "Starting mDNS");
        ESP_ERROR_CHECK(mdns_init());
        ESP_ERROR_CHECK(mdns_hostname_set("bugcam"));
        ESP_ERROR_CHECK(mdns_instance_name_set("BugCam ESP32S3"));
        ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
        ESP_LOGI(TAG, "mDNS responder started: http://bugcam.local/");
        mdns_started = true;
    }
}

// Brings up NVS, TCP/IP, and the default event loop.
void initWifiRadio(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

// Connects to the physical HaLow router over Sub-1 GHz (WPA3-SAE).
void initWifiHaLow(void)
{
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, nullptr));

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(mmhalow_init(&init_cfg));

    mmhalow_wifi_config_t cfg = {};
    cfg.sta = MMWLAN_STA_ARGS_INIT;
    strlcpy(reinterpret_cast<char *>(cfg.sta.ssid), HALOW_SSID, sizeof(cfg.sta.ssid));
    cfg.sta.ssid_len = strlen(HALOW_SSID);
    strlcpy(reinterpret_cast<char *>(cfg.sta.passphrase), HALOW_PASS, sizeof(cfg.sta.passphrase));
    cfg.sta.passphrase_len = strlen(HALOW_PASS);
    cfg.sta.security_type = MMWLAN_SAE;

    ESP_ERROR_CHECK(mmhalow_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(mmhalow_connect(nullptr));

    ESP_LOGI(TAG, "HaLow STA connecting to SSID: %s", HALOW_SSID);
}