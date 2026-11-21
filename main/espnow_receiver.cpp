#include <string.h>
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "board_config.h"
#include "command_protocol.h"
static const char *TAG  = "espnow";

// ---------- DISCOVERY ----------
// Base station broadcasts "DISCOVER"; we reply with a unicast "ANNOUNCE".
// Base station only checks the ANNOUNCE prefix, so fields can be appended later.
static const uint8_t DISCOVER_MSG[] = {'D','I','S','C','O','V','E','R'};
static const uint8_t ANNOUNCE_MSG[] = {'A','N','N','O','U','N','C','E'};
static const uint32_t ANNOUNCE_MAX_JITTER_MS = 200;   // well inside base's 1000 ms scan window

// Base station MAC, learned from the most recent DISCOVER. Only written by discoveryTask.
static uint8_t baseMac[ESP_NOW_ETH_ALEN] = {0};
static bool    baseKnown = false;

// recv callback runs in the WiFi task and must not block, so it hands the
// DISCOVER sender's MAC to discoveryTask, which does the delay + reply.
static QueueHandle_t discoverQueue = nullptr;

// ESP-NOW only works if both ends sit on the same channel. Returns false
// (and logs) if the radio has drifted off WIFI_CHANNEL, e.g. by joining an AP.
static bool checkChannel(void) {
    uint8_t primary = 0;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&primary, &second) != ESP_OK) {
        ESP_LOGW(TAG, "Could not read WiFi channel");
        return false;
    }
    if (primary != WIFI_CHANNEL) {
        ESP_LOGE(TAG, "WiFi on channel %u, expected %u - ESP-NOW to base station will fail",
                 (unsigned)primary, (unsigned)WIFI_CHANNEL);
        return false;
    }
    return true;
}

// Joining an AP forces the radio onto the AP's channel, overriding WIFI_CHANNEL.
static void onStaConnected(void *arg, esp_event_base_t base, int32_t id, void *data) {
    const wifi_event_sta_connected_t *ev = (const wifi_event_sta_connected_t *)data;
    ESP_LOGE(TAG, "STA joined an AP on channel %u - this overrides ESP-NOW channel %u",
             (unsigned)ev->channel, (unsigned)WIFI_CHANNEL);
}

static void sendAnnounce(const uint8_t *mac) {
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
        peer.channel = WIFI_CHANNEL;
        peer.ifidx   = WIFI_IF_STA;
        peer.encrypt = false;
        esp_err_t err = esp_now_add_peer(&peer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Add base peer failed: %s", esp_err_to_name(err));
            return;
        }
    }

    esp_err_t err = esp_now_send(mac, ANNOUNCE_MSG, sizeof(ANNOUNCE_MSG));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ANNOUNCE send failed: %s", esp_err_to_name(err));
    }
}

static void discoveryTask(void *pvParameters) {
    uint8_t mac[ESP_NOW_ETH_ALEN];

    for (;;) {
        if (xQueueReceive(discoverQueue, mac, portMAX_DELAY) != pdTRUE) continue;

        memcpy(baseMac, mac, ESP_NOW_ETH_ALEN);
        baseKnown = true;

        // Random back-off so multiple Device Bs answering the same DISCOVER don't collide.
        uint32_t jitterMs = esp_random() % ANNOUNCE_MAX_JITTER_MS;
        vTaskDelay(pdMS_TO_TICKS(jitterMs));

        checkChannel();
        ESP_LOGI(TAG, "DISCOVER from %02X:%02X:%02X:%02X:%02X:%02X, ANNOUNCE after %u ms",
                 baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5],
                 (unsigned)jitterMs);
        sendAnnounce(baseMac);
    }
}

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    ESP_LOGI(TAG, "ESP-NOW TX %s", status == ESP_NOW_SEND_SUCCESS ? "ACK" : "FAIL");
}

void onDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len);

void onDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len) {
    const uint8_t *mac_addr = esp_now_info -> src_addr;

    ESP_LOGI(TAG, "ESP-NOW RX LEN = %d", len);

    // Answer every DISCOVER - the base station clears its table on each scan.
    if (len >= (int)sizeof(DISCOVER_MSG) && memcmp(data, DISCOVER_MSG, sizeof(DISCOVER_MSG)) == 0) {
        if (xQueueSend(discoverQueue, mac_addr, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Discovery queue full, dropping DISCOVER");
        }
        return;
    }

    if (len != sizeof(uint32_t)){
        ESP_LOGI(TAG, "Unexpected Length, Ignoring");
        return;
    }

    uint32_t code;
    memcpy(&code, data, sizeof(code));

    StimCmd cmd;
    if (!decodeCommand(code, cmd, mac_addr)){
        ESP_LOGI(TAG, "Decoded Command Invalid: Ignoring");
        return;
    }

    enqueueCmdFromISR(cmd);
}


void initESPNow() {
    if (esp_now_init() != ESP_OK) {
        ESP_LOGI(TAG, "Error Initialising ESP-NOW");
        while(true){ vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    discoverQueue = xQueueCreate(4, ESP_NOW_ETH_ALEN);
    if (discoverQueue == nullptr ||
        xTaskCreatePinnedToCore(discoveryTask, "Discovery", 3072, nullptr, 3, nullptr, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start discovery task");
    }

    checkChannel();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, onStaConnected, nullptr));

    esp_now_register_recv_cb(onDataRecv);
    esp_now_register_send_cb(onDataSent);
}
