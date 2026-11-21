#include "command_protocol.h"
#include "board_config.h"
#include "freertos\FreeRTOS.h"
#include "freertos\portmacro.h"
#include <string.h>

static portMUX_TYPE cmdQueueMux = portMUX_INITIALIZER_UNLOCKED;


StimCmd cmdQueue[CMD_QUEUE_SIZE];
volatile uint8_t cmdHead     = 0;
volatile uint8_t cmdTail     = 0;
volatile bool    cmdOverflow = false;
uint32_t         lastCode    = 0;


// ---------- QUEUE ----------
bool enqueueCmdFromISR(const StimCmd& cmd) {
  portENTER_CRITICAL_ISR(&cmdQueueMux); 
  uint8_t nextHead = (cmdHead + 1) % CMD_QUEUE_SIZE;
  if (nextHead == cmdTail) {
    cmdOverflow = true;
    portEXIT_CRITICAL_ISR(&cmdQueueMux);
    return false;
  }
  cmdQueue[cmdHead] = cmd;
  cmdHead = nextHead;
  portEXIT_CRITICAL_ISR(&cmdQueueMux);
  return true;
}


bool dequeueCmd(StimCmd& out) {
  portENTER_CRITICAL(&cmdQueueMux);
  if (cmdTail == cmdHead) {
    portEXIT_CRITICAL(&cmdQueueMux);
    return false;
  }
  out = cmdQueue[cmdTail];
  cmdTail = (cmdTail + 1) % CMD_QUEUE_SIZE;
  portEXIT_CRITICAL(&cmdQueueMux);
  return true;
}


// ---------- COMMAND DECODE ----------
bool decodeCommand(uint32_t code, StimCmd& cmd, const uint8_t *srcMac) {
  uint8_t siteNibble  = (code >> 24) & 0x0F;
  uint8_t pwRaw       = (code >> 16) & 0xFF;
  uint8_t durationRaw = (code >> 8)  & 0xFF;
  uint8_t freqRaw     = code & 0xFF;

  char siteChar = "0123456789ABCDEF"[siteNibble];
  
  uint16_t durationMs = (uint16_t)durationRaw * 10U;
  uint16_t freqHz = (uint16_t)freqRaw;
  uint8_t pwMs = pwRaw;

  uint16_t periodMs = 1000U / freqHz;
  if (periodMs < 2) periodMs = 2;


  // Must be strictly less than period to avoid underflow in low phase time
  if (pwMs >= periodMs) {
    pwMs = periodMs - 1;
  }

  if (pwMs == 0) {
    return false;
  }

  cmd.site = siteChar;
  cmd.durationMs = durationMs;
  cmd.freqHz = freqHz;
  cmd.pwMs = pwMs;
  cmd.rawCode = code;

  if (srcMac) {
    memcpy(cmd.srcMac, srcMac, 6);
  } else {
    memset(cmd.srcMac, 0, 6);
  }

  return true;
}