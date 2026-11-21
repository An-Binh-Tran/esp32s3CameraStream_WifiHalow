#pragma once

// Brings up just the WiFi radio (NVS, netif, event loop, STA mode, fixed
// channel) - enough for ESP-NOW to send/receive. No AP broadcast, no mDNS.
// Call this once at boot, before initESPNow().
void initWifiRadio(void);

// Upgrades the radio to APSTA and starts broadcasting the SoftAP + mDNS.
// Requires initWifiRadio() to have already run. Call before initServer().
void initWifiAP(void);
