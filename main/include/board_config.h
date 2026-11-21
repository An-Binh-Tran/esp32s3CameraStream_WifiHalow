#pragma once
#include "driver/gpio.h"
#include <cstdint>

#define CAMERA_MODEL_XIAO_ESP32S3
#define INPUT 1
#define OUTPUT 0

void init_led(void); 
void init_stim_pins(void); 
constexpr gpio_num_t R_PIN = GPIO_NUM_6;  // Silkscreen D5
constexpr gpio_num_t L_PIN = GPIO_NUM_7;  // Silkscreen D8
constexpr gpio_num_t RE_PIN = GPIO_NUM_5; // Silkscreen D4
constexpr gpio_num_t LE_PIN = GPIO_NUM_8; // Silkscreen D9
constexpr gpio_num_t LED_PIN = GPIO_NUM_21;

constexpr uint8_t IDLE_PIN_MODE  = INPUT;
constexpr uint8_t IDLE_PIN_LEVEL = 0;
constexpr bool STIM_PREEMPT_ACTIVE = true;
constexpr uint8_t CMD_QUEUE_SIZE   = 3;

constexpr uint8_t WIFI_CHANNEL    = 6;
constexpr const char* AP_SSID     = "XIAO-CAM-3";
constexpr const char* AP_PASS     = "cam12345";


struct StimCmd {
    char site; 
    uint16_t durationMs; 
    uint16_t freqHz; 
    uint8_t pwMs; 
    uint32_t rawCode; 
    uint8_t srcMac[6];
};