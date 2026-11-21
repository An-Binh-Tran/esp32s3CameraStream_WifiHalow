#pragma once 
#include "board_config.h"

bool enqueueCmdFromISR(const StimCmd& cmd); 
bool dequeueCmd(StimCmd& out); 
bool decodeCommand(uint32_t code, StimCmd& cmd, const uint8_t *srcMac); 
 