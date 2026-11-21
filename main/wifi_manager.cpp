#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "mdns.h"
#include "board_config.h"
#include "wifi_manager.h"  // for AP_SSID, AP_PASS, WIFI_CHANNEL, etc.

static const char *TAG = "wifi_manager";

// AP netif handle, needed by initWifiAP() to read back the AP's IP once
// it's actually broadcasting. Set by initWifiRadio().
static esp_netif_t *ap_netif = nullptr;

// ---------- WIFI RADIO ----------
// Brings up just enough Wi-Fi to make ESP-NOW usable: NVS, the network
// stack/event loop, the driver itself, and STA mode on a fixed channel.
// Does NOT broadcast a SoftAP or start any server - safe to call even when
// the camera isn't present. Call this once at boot, before initESPNow().
void initWifiRadio(void)
{
    // NVS (Non-Volatile Storage) is flash-based storage that the Wi-Fi
    // driver uses internally to save settings. It must be initialised
    // before Wi-Fi will work at all.
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // The NVS partition is full or from an incompatible older version -
        // wipe it and start fresh, then try initialising again.
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Set up the underlying network stack (TCP/IP) and the system event
    // loop, which Wi-Fi/networking components use to report things like
    // "got an IP address" or "client connected".
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create both netifs up front so initWifiAP() can upgrade into AP mode
    // later without needing to touch netif setup again.
    esp_netif_create_default_wifi_sta();
    ap_netif = esp_netif_create_default_wifi_ap();

    // Initialise the Wi-Fi driver itself with its default settings
    // (buffer sizes, task priorities, etc).
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // STA-only for now - no AP broadcast, no HTTP server. initWifiAP()
    // upgrades this to APSTA when the camera is actually present.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());

    // With no AP config and no STA connection, the radio has no fixed
    // channel yet. Pin it to WIFI_CHANNEL so ESP-NOW peers (which transmit
    // on that fixed channel) can actually be heard.
    ESP_ERROR_CHECK(esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Log this board's own Wi-Fi station MAC address - useful when pairing
    // it with another device over ESP-NOW, which identifies peers by MAC.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "WiFi radio up (STA-only, channel %d), MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             WIFI_CHANNEL, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// ---------- WIFI AP + SERVER ----------
// Upgrades the radio (already brought up by initWifiRadio()) into a SoftAP
// (the board creates its own Wi-Fi network that phones/laptops can join
// directly - no router needed) and starts mDNS so the board can be reached
// at "bugcam.local" instead of a raw IP address. Call before initServer().
void initWifiAP(void)
{
    // Fill in the SoftAP (Access Point) settings: the Wi-Fi network this
    // board broadcasts for other devices to connect to.
    wifi_config_t ap_config = {};
    strcpy((char *)ap_config.ap.ssid, AP_SSID);       // network name, e.g. "XIAO-CAM-3"
    strcpy((char *)ap_config.ap.password, AP_PASS);   // network password
    ap_config.ap.ssid_len       = strlen(AP_SSID);
    ap_config.ap.channel        = WIFI_CHANNEL;       // Wi-Fi channel to broadcast on
    ap_config.ap.max_connection = 4;                  // allow up to 4 devices connected at once
    // If no password was set, run as an open network; otherwise require
    // WPA2 with the given password.
    ap_config.ap.authmode       = (strlen(AP_PASS) == 0) ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    ap_config.ap.beacon_interval = 100;  // how often (ms) the AP announces itself - default value

    // Switch on AP broadcasting alongside the existing STA mode first -
    // esp_wifi_set_config(WIFI_IF_AP, ...) requires the driver to already
    // be in an AP-capable mode, otherwise it fails with ESP_ERR_WIFI_MODE.
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    // Read back the IP address the AP interface ended up with (this is
    // normally a fixed default like 192.168.4.1 for SoftAP mode), just to
    // log it for the user.
    esp_netif_ip_info_t ap_ip;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(ap_netif, &ap_ip));

    ESP_LOGI(TAG, "AP started, SSID: %s, PASS: %s", AP_SSID, AP_PASS);
    ESP_LOGI(TAG, "AP IP: " IPSTR, IP2STR(&ap_ip.ip));

    // mDNS lets other devices on the network find this board by a friendly
    // name ("bugcam.local") instead of needing to know its IP address.
    ESP_LOGI(TAG, "Starting mDNS");
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("bugcam"));       // -> reachable at bugcam.local
    ESP_ERROR_CHECK(mdns_instance_name_set("BugCam ESP32S3"));

    // Advertise that this board offers an HTTP service on port 80, so
    // mDNS-aware tools/browsers can discover the web/video server automatically.
    ESP_ERROR_CHECK(mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0));
    ESP_LOGI(TAG, "mDNS responder started: http://bugcam.local/");
}
