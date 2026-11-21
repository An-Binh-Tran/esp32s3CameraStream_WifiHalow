#pragma once

// Registers the ESP-NOW receive callback that decodes incoming packets into
// StimCmd entries on the stim queue. Requires the WiFi radio to already be
// up (see initWifiRadio()) - ESP-NOW rides on the WiFi driver.
void initESPNow(void);
