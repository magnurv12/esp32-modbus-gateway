#pragma once

#include <ESPAsyncWebServer.h>

#include <utility>
#include <vector>

#include "poll_scheduler.h"

// Live data over WebSocket at /ws. Clients send JSON "subscribe" /
// "unsubscribe" messages; modbusTask polls the subscribed ranges and pushes
// a snapshot, then only the addresses that changed. Protocol reference:
// docs/websocket.md.

constexpr size_t WS_MAX_CLIENTS = 4;
constexpr size_t WS_MAX_MESSAGE_BYTES = 512;

void registerWsStream(AsyncWebServer &server);

// Closes clients over the limit and frees dead ones. Call about once a
// second from loop().
void wsMaintenance();

size_t wsClientCount();

// ---- Sending (called from modbusTask) ----

// False when the client is gone or its send queue is full.
bool wsCanSend(uint32_t clientId);

// {"type": type, "id", "effectiveIntervalMs"} -- "subscribed" on accept,
// "interval" when the bus budget later changes the effective interval.
void wsSendInterval(const Subscription &sub, const char *type);
void wsSendUnsubscribed(uint32_t clientId, const char *id);
void wsSendSnapshot(const Subscription &sub, const uint16_t *values, uint32_t readAt);
void wsSendUpdate(const Subscription &sub, const std::vector<std::pair<uint16_t, uint16_t>> &changes,
                  uint32_t readAt);
// A poll of the subscription's range failed.
void wsSendModbusError(const Subscription &sub, uint8_t result, uint32_t retryInMs);
// A request-level error (bad message, limits). `id` may be nullptr.
void wsSendError(uint32_t clientId, const char *id, const char *error, const char *message);
