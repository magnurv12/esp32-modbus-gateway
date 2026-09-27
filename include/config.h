#pragma once

#include <Arduino.h>

// ---- WiFi ----
// WIFI_SSID / WIFI_PASSWORD live in include/secrets.h, which is git-ignored.
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Missing include/secrets.h: copy include/secrets.example.h to include/secrets.h and set your WiFi credentials"
#endif

constexpr const char *MDNS_HOSTNAME = "modbus-gateway";

// ---- RS-485 / Modbus RTU ----
constexpr uint32_t MODBUS_BAUD = 9600;
// Slave addressed when a request doesn't pass `?slave=`.
constexpr uint8_t DEFAULT_SLAVE_ID = 1;
constexpr int DE_RE_PIN = 23;
constexpr int RS485_RX_PIN = 16; // RO
constexpr int RS485_TX_PIN = 17; // DI
// How long to wait for a slave's reply. ModbusMaster's default is 2000 ms;
// shorter keeps a dead slave from stalling live polling. At 9600 baud a
// 125-register reply takes ~270 ms, so don't go below ~350.
constexpr uint16_t MODBUS_RESPONSE_TIMEOUT_MS = 400;

// ---- Debug ----
constexpr uint32_t DEBUG_BAUD = 115200;
constexpr int LED_PIN = 2; // blinks on each valid Modbus reply (dark = no traffic)
constexpr uint32_t LED_FLASH_MS = 40;

// ---- HTTP server ----
constexpr uint16_t HTTP_PORT = 80;
// Origin allowed to call the REST API from a browser page served elsewhere
// (a front-end on another host or a dev server such as localhost:5173).
// "*" allows any page; set e.g. "http://192.168.1.10:5173" to allow only
// that one, or "" to disable CORS. WebSocket (/ws) isn't subject to CORS.
constexpr const char *CORS_ALLOW_ORIGIN = "*";
