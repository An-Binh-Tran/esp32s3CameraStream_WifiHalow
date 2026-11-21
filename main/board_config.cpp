#include "driver/gpio.h"
#include "board_config.h"

void init_led(void) {
    gpio_config_t led_conf = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&led_conf);
}

void init_stim_pins(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << R_PIN) | (1ULL << L_PIN) 
         | (1ULL << RE_PIN) | (1ULL << LE_PIN),
        .mode = GPIO_MODE_INPUT, 
        .pull_up_en = GPIO_PULLUP_DISABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    }; 
    gpio_config(&io_conf);
}