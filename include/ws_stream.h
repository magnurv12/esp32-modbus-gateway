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

// False when the client's send queue is full or control replies are
// still waiting for it (data must not overtake them).
bool wsCanSend(uint32_t clientId);

// Sends the control replies that were queued because a client's send
// queue was full. Call regularly, from modbusTask only.
void wsFlushReplies();

// Control replies -- never dropped: queued while the client's send queue
// is full. wsSendError is also called from the async_tcp task.
// {"type": type, "id", "effectiveIntervalMs"} -- "subscribed" on accept,
// "interval" when the bus budget later changes the effective interval.
void wsSendInterval(const Subscription &sub, const char *type);
void wsSendUnsubscribed(uint32_t clientId, const char *id);
// A request-level error (bad message, limits). `id` may be nullptr.
void wsSendError(uint32_t clientId, const char *id, const char *error, const char *message);

// Data -- callers check wsCanSend() first and skip when it says no: only
// the latest values matter, and the scheduler resends a full snapshot
// once the client catches up.
void wsSendSnapshot(const Subscription &sub, const uint16_t *values, uint32_t readAt);
void wsSendUpdate(const Subscription &sub, const std::vector<std::pair<uint16_t, uint16_t>> &changes,
                  uint32_t readAt);
// A poll of the subscription's range failed.
void wsSendModbusError(const Subscription &sub, uint8_t result, uint32_t retryInMs);
