#include "driver/gpio.h"
#include "board_config.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


struct StimState {
    bool active = false; 
    bool highPhase = false; 
    gpio_num_t pin1 = GPIO_NUM_NC; 
    gpio_num_t pin2 = GPIO_NUM_NC; 
    uint32_t periodMs = 1; 
    uint32_t highMs = 1; 
    uint32_t pulsesTotal = 0; 
    uint32_t pulsesDone = 0; 
    uint32_t nextToggleMs = 0; 
}; 

StimState stimState; 


static inline uint32_t millis(void) { 
    return (uint32_t) xTaskGetTickCount(); //1 tick == 1ms, same grid as vTaskDelay
}

static_assert(configTICK_RATE_HZ == 1000, "Stim timing assumes 1 ms ticks");

// Helper Functions //

static void setIdlePin(gpio_num_t pin) { 
    if (pin == GPIO_NUM_NC) return; 

    if (IDLE_PIN_MODE == INPUT) { 
        gpio_set_direction(pin, GPIO_MODE_INPUT); 
    } else {
        gpio_set_direction(pin, GPIO_MODE_OUTPUT); 
        gpio_set_level(pin, IDLE_PIN_LEVEL); 
    }
}

bool stimBusy(void) {
    return stimState.active; 
}

void abortStim() {
  if (stimState.active) {
    if (stimState.pin1) setIdlePin(stimState.pin1);
    if (stimState.pin2) setIdlePin(stimState.pin2);
    stimState.active = false;
  }

  gpio_set_level(LED_PIN, 1);
}

void setStimPinsToIdle() {
  setIdlePin(L_PIN);
  setIdlePin(R_PIN);
  setIdlePin(LE_PIN);
  setIdlePin(RE_PIN);
}


void startStimTrainSingle(gpio_num_t pin, uint16_t durationMs, uint16_t freqHz, uint8_t pwMs) { 
    if (freqHz == 0 || durationMs == 0) return; 

    uint32_t period = 1000UL / freqHz; 
    if(period < 2) period = 2; 

    if (pwMs >= period) { 
        pwMs = period - 1; 
        if (pwMs <= 0) return; 
    }

    uint32_t pulses = durationMs / period; 
    if (pulses == 0) return; 

    stimState.active = true; 
    stimState.highPhase = true; 
    stimState.pin1 = pin; 
    stimState.pin2 = GPIO_NUM_NC; 
    stimState.periodMs = period; 
    stimState.highMs = pwMs; 
    stimState.pulsesTotal = pulses; 
    stimState.pulsesDone = 0; 
    stimState.nextToggleMs = millis() + stimState.highMs;  

    // Here we set pin1 high and LED high 
    gpio_set_direction(stimState.pin1, GPIO_MODE_OUTPUT); 
    gpio_set_level(stimState.pin1, 1); 
    gpio_set_level(LED_PIN, 0); 
}


void startStimTrainDual(gpio_num_t pinA, gpio_num_t pinB, uint16_t durationMs, uint16_t freqHz, uint8_t pwMs) { 
    if (freqHz == 0 || durationMs == 0) return; 

    uint32_t period = 1000UL / freqHz; 
    if(period < 2) period = 2; 

    if (pwMs >= period) { 
        pwMs = period - 1; 
        if (pwMs <= 0) return; 
    }

    uint32_t pulses = durationMs / period; 
    if (pulses == 0) return; 

    stimState.active = true; 
    stimState.highPhase = true; 
    stimState.pin1 = pinA; 
    stimState.pin2 = pinB; 
    stimState.periodMs = period; 
    stimState.highMs = pwMs; 
    stimState.pulsesTotal = pulses; 
    stimState.pulsesDone = 0; 
    stimState.nextToggleMs = millis() + stimState.highMs;  

    // Here we set pin1 high and LED high 
    gpio_set_direction(stimState.pin1, GPIO_MODE_OUTPUT); 
    gpio_set_direction(stimState.pin2, GPIO_MODE_OUTPUT); 
    gpio_set_level(stimState.pin1, 1); 
    gpio_set_level(stimState.pin2, 1); 
    gpio_set_level(LED_PIN, 0); 
}



void serviceStimTrain() { 
    if (!stimState.active) return; 

    uint32_t now = millis(); 

    if ((int32_t)(now - stimState.nextToggleMs) < 0) { 
        return; 
    }

    if (stimState.highPhase) { 
        if (stimState.pin1 != GPIO_NUM_NC) gpio_set_level(stimState.pin1, 0); 
        if (stimState.pin2 != GPIO_NUM_NC) gpio_set_level(stimState.pin2, 0); 
        
        gpio_set_level(LED_PIN, 1); 

        stimState.highPhase = false; 

        uint32_t lowMs = stimState.periodMs - stimState.highMs; 
        if (lowMs < 1) lowMs = 1;                   

        stimState.nextToggleMs += lowMs; 
    } else { 
        stimState.pulsesDone++; 

        if (stimState.pulsesDone >= stimState.pulsesTotal) { 
            if (stimState.pin1 != GPIO_NUM_NC) setIdlePin(stimState.pin1); 
            if (stimState.pin2 != GPIO_NUM_NC) setIdlePin(stimState.pin2); 

            stimState.active = false; 

            gpio_set_level(LED_PIN, 1); 
            return; 
        }

        if (stimState.pin1 != GPIO_NUM_NC) gpio_set_level(stimState.pin1, 1); 
        if (stimState.pin2 != GPIO_NUM_NC) gpio_set_level(stimState.pin2, 1); 
        
        gpio_set_level(LED_PIN, 0); 
        stimState.highPhase = true; 
        stimState.active = true; 
        stimState.nextToggleMs = now + stimState.highMs; 
    }
}



void startCommand(const StimCmd &cmd) {
  switch (cmd.site) {
    case 'A':
      startStimTrainSingle(L_PIN, cmd.durationMs, cmd.freqHz, cmd.pwMs);
      break;
    case 'B':
      startStimTrainSingle(R_PIN, cmd.durationMs, cmd.freqHz, cmd.pwMs);
      break;
    case 'C':
      startStimTrainSingle(LE_PIN, cmd.durationMs, cmd.freqHz, cmd.pwMs);
      break;
    case 'D':
      startStimTrainSingle(RE_PIN, cmd.durationMs, cmd.freqHz, cmd.pwMs);
      break;
    case 'E':
      startStimTrainDual(LE_PIN, RE_PIN, cmd.durationMs, cmd.freqHz, cmd.pwMs);
      break;
    default:
      break;
  }
}