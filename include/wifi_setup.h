#pragma once

// Blocks (with periodic debug prints + LED blink) until connected to
// WIFI_SSID, then starts mDNS advertising MDNS_HOSTNAME.local.
void connectWifiAndMdns();
