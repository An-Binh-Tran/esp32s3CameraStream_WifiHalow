#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_config.h"
#include "stim_controller.h"
#include "camera_server.h"
#include "wifi_manager.h"
#include "espnow_receiver.h"
#include "command_protocol.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"


static const char *TAG  = "main";
static TaskHandle_t stimTaskHandle = nullptr;

void key_press_indi(void) {
  for (int i = 0; i < 4; i++)
  {
    gpio_set_level(LED_PIN, 0); 
    vTaskDelay(pdMS_TO_TICKS(200)); 
    gpio_set_level(LED_PIN, 1); 
    vTaskDelay(pdMS_TO_TICKS(200)); 
  }
}

void printMac(const uint8_t *mac) { 
  if (!mac) return; 
  ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2],
                  mac[3], mac[4], mac[5]); 
}



static void stimTask(void *pvParameters)
{
    ESP_LOGI(TAG, "StimTask running on Core %d", xPortGetCoreID());

    for (;;) {
        // Must be non-blocking: update the current stimulation train,
        // toggle GPIO if the next timing event has arrived, then return.
        serviceStimTrain();

        StimCmd cmd;

        if (dequeueCmd(cmd)) {
            if (stimBusy()) {
                if (STIM_PREEMPT_ACTIVE) {
                    abortStim();
                    startCommand(cmd);
                } else {
                    ESP_LOGW(TAG, "Stim busy; discarding incoming command");
                }
            } else {
                startCommand(cmd);
            }
        }

        // Yield to other Core-1 tasks. Adjust later if required for
        // stimulation-timing resolution.
        vTaskDelay(1);
    }
}



extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));

    init_led();
    gpio_set_level(LED_PIN, 1);

    // init_stim_pins();
    // setStimPinsToIdle();

    key_press_indi();



    // ESP_LOGI(TAG, "PSRAM total: %u bytes",
    //         (unsigned)esp_psram_get_size());

    // ESP_LOGI(TAG, "PSRAM free:  %u bytes",
    //      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const bool cameraOK = initCamera();

    // Always bring up just enough WiFi radio for ESP-NOW, so stim commands
    // can be received whether or not the camera is present.
    initWifiRadio();
    // initESPNow();
initWifiHaLow();

    if (cameraOK) {
        ESP_LOGI(TAG, "Camera initialized successfully");
        initServer();
    } else {
        ESP_LOGW(TAG, "Camera unavailable; HaLow is active, HTTP server skipped");
    }

    // BaseType_t taskResult = xTaskCreatePinnedToCore(
    //     stimTask,          // Task function
    //     "StimTask",        // Name shown in debugging/task listings
    //     4096,              // Stack size in bytes for ESP-IDF
    //     nullptr,           // No task parameter
    //     5,                 // Priority
    //     &stimTaskHandle,   // Store task handle
    //     1                  // Pin to Core 1
    // );

    // if (taskResult != pdPASS) {
    //     ESP_LOGE(TAG, "Failed to create StimTask");
    //     return;
    // }

    ESP_LOGI(TAG, "StimTask created and pinned to Core 1"); 
}