#pragma once

// Template for include/secrets.h, which holds this device's WiFi
// credentials and is git-ignored so they never reach the repository:
//
//   cp include/secrets.example.h include/secrets.h
//
// then edit include/secrets.h. The ESP32 only joins 2.4 GHz networks.

constexpr const char *WIFI_SSID = "your-2.4GHz-network";
constexpr const char *WIFI_PASSWORD = "your-password";
