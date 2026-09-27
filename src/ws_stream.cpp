#include "ws_stream.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <map>

#include "config.h"
#include "json_codec.h"
#include "modbus_task.h"

namespace {

AsyncWebSocket ws("/ws");

// Disconnects whose ClientGone command didn't fit in the queue; retried
// from wsMaintenance() so their subscriptions never keep polling.
SemaphoreHandle_t pendingGoneMutex = nullptr;
std::vector<uint32_t> pendingGone;

// Per-client state shared by the async_tcp task (events), modbusTask
// (sending) and loop() (cleanup), guarded by clientStateMutex.
//
// Lock order: the library raises CONNECT / DISCONNECT events while holding
// its own client-list lock, and those handlers take clientStateMutex. So
// no ws.* call may ever be made while holding clientStateMutex.
SemaphoreHandle_t clientStateMutex = nullptr;

// Control replies (subscribed, interval, unsubscribed, errors) are sent
// once and never repeated, so they must not be lost when a slow client's
// send queue is full (the library would drop them silently). They wait
// here, oldest first, and are sent by wsFlushReplies() -- only ever called
// from modbusTask, so a reply is never sent twice. While a client has
// replies waiting, wsCanSend() says no, so data can't overtake them.
struct PendingReply {
  uint32_t clientId;
  String text;
};
std::vector<PendingReply> outbox;
// A client this far behind won't catch up: disconnect it (it reconnects
// and subscribes again from a clean state).
constexpr size_t kMaxPendingRepliesPerClient = 16;

// Text frames that arrived in pieces (the frame spanned TCP segments),
// by client.
std::map<uint32_t, String> partialMessages;

size_t pendingRepliesLocked(uint32_t clientId) {
  return std::count_if(outbox.begin(), outbox.end(),
                       [clientId](const PendingReply &reply) { return reply.clientId == clientId; });
}

void sendDoc(uint32_t clientId, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  ws.text(clientId, out);
}

// Sends a control reply now if the client can take it and has nothing
// queued before it; queues it otherwise.
void sendReply(uint32_t clientId, JsonDocument &doc) {
  String out;
  serializeJson(doc, out);

  xSemaphoreTake(clientStateMutex, portMAX_DELAY);
  bool queued = pendingRepliesLocked(clientId) > 0;
  xSemaphoreGive(clientStateMutex);
  if (!queued && ws.availableForWrite(clientId)) {
    ws.text(clientId, out);
    return;
  }

  xSemaphoreTake(clientStateMutex, portMAX_DELAY);
  bool overflow = pendingRepliesLocked(clientId) >= kMaxPendingRepliesPerClient;
  if (!overflow) outbox.push_back({clientId, std::move(out)});
  xSemaphoreGive(clientStateMutex);
  if (overflow) ws.close(clientId);
}

void forgetClient(uint32_t clientId) {
  xSemaphoreTake(clientStateMutex, portMAX_DELAY);
  outbox.erase(std::remove_if(outbox.begin(), outbox.end(),
                              [clientId](const PendingReply &reply) { return reply.clientId == clientId; }),
               outbox.end());
  partialMessages.erase(clientId);
  xSemaphoreGive(clientStateMutex);
}

// Adds one piece of a text frame. True once the frame is complete, with
// the whole message in `message`.
bool appendFramePiece(uint32_t clientId, const AwsFrameInfo &info, const uint8_t *data, size_t len,
                      String &message) {
  xSemaphoreTake(clientStateMutex, portMAX_DELAY);
  String &buffer = partialMessages[clientId];
  if (info.index == 0) buffer = String();
  bool complete = false;
  if (buffer.length() != info.index) {
    partialMessages.erase(clientId); // a piece went missing: drop the frame
  } else {
    buffer.concat(reinterpret_cast<const char *>(data), len);
    if (buffer.length() >= info.len) {
      message = std::move(buffer);
      partialMessages.erase(clientId);
      complete = true;
    }
  }
  xSemaphoreGive(clientStateMutex);
  return complete;
}

void addSubscriptionFields(JsonDocument &doc, const Subscription &sub) {
  doc["id"] = sub.id;
  doc["table"] = tableName(sub.table);
  doc["slave"] = sub.slave;
}

void submitClientGone(uint32_t clientId) {
  StreamCommand command{};
  command.kind = StreamCommandKind::ClientGone;
  command.request.clientId = clientId;
  if (submitStreamCommand(command)) return;
  xSemaphoreTake(pendingGoneMutex, portMAX_DELAY);
  pendingGone.push_back(clientId);
  xSemaphoreGive(pendingGoneMutex);
}

void sendHello(AsyncWebSocketClient *client) {
  JsonDocument doc;
  doc["type"] = "hello";
  doc["clientId"] = client->id();
  doc["defaultSlave"] = DEFAULT_SLAVE_ID;
  JsonObject limits = doc["limits"].to<JsonObject>();
  limits["subscriptionsPerConnection"] = WS_MAX_SUBSCRIPTIONS_PER_CLIENT;
  limits["minIntervalMs"] = WS_MIN_INTERVAL_MS;
  limits["maxIntervalMs"] = WS_MAX_INTERVAL_MS;
  limits["maxRegisters"] = MODBUS_MAX_READ_REGISTERS;
  limits["maxBits"] = MODBUS_MAX_READ_BITS;
  sendReply(client->id(), doc);
}

// Reads an unsigned integer field in [min, max]; absent -> `fallback`
// unless `required`. Replies with an error and returns false otherwise.
bool readUintField(uint32_t clientId, const char *subId, JsonVariantConst msg, const char *name,
                   uint32_t min, uint32_t max, bool required, uint32_t fallback, uint32_t &out) {
  JsonVariantConst field = msg[name];
  if (field.isNull() && !required) {
    out = fallback;
    return true;
  }
  if (!field.is<uint32_t>() || field.as<uint32_t>() < min || field.as<uint32_t>() > max) {
    String message = String("'") + name + "' must be an integer from " + min + " to " + max;
    wsSendError(clientId, subId, "invalid_parameter", message.c_str());
    return false;
  }
  out = field.as<uint32_t>();
  return true;
}

// Validates the subscription id; replies with an error when invalid.
bool readIdField(uint32_t clientId, JsonVariantConst msg, const char *&id) {
  id = msg["id"].as<const char *>();
  size_t length = id == nullptr ? 0 : strlen(id);
  if (length == 0 || length > WS_SUBSCRIPTION_ID_MAX) {
    wsSendError(clientId, nullptr, "invalid_parameter",
                "'id' must be a string of 1 to 32 characters");
    return false;
  }
  return true;
}

void handleSubscribe(uint32_t clientId, JsonVariantConst msg) {
  const char *id;
  if (!readIdField(clientId, msg, id)) return;

  ModbusTable table;
  if (!parseTableName(msg["table"].as<const char *>(), table)) {
    wsSendError(clientId, id, "invalid_parameter",
                "'table' must be one of holding, input, coils, discrete");
    return;
  }
  uint16_t maxCount = maxReadCount(table);
  uint32_t slave, start, count, interval;
  if (!readUintField(clientId, id, msg, "slave", MODBUS_MIN_SLAVE_ID, MODBUS_MAX_SLAVE_ID, false,
                     DEFAULT_SLAVE_ID, slave) ||
      !readUintField(clientId, id, msg, "start", 0, 0xFFFF, true, 0, start) ||
      !readUintField(clientId, id, msg, "count", 1, maxCount, true, 0, count) ||
      !readUintField(clientId, id, msg, "intervalMs", WS_MIN_INTERVAL_MS, WS_MAX_INTERVAL_MS, false,
                     WS_DEFAULT_INTERVAL_MS, interval)) {
    return;
  }
  if (start + count > 0x10000) {
    wsSendError(clientId, id, "invalid_parameter", "start + count must not exceed 65536");
    return;
  }

  StreamCommand command{};
  command.kind = StreamCommandKind::Subscribe;
  command.request.clientId = clientId;
  strlcpy(command.request.id, id, sizeof(command.request.id));
  command.request.slave = slave;
  command.request.table = table;
  command.request.start = start;
  command.request.count = count;
  command.request.intervalMs = interval;
  if (!submitStreamCommand(command)) {
    wsSendError(clientId, id, "busy", "Gateway is busy, try again shortly");
  }
}

void handleUnsubscribe(uint32_t clientId, JsonVariantConst msg) {
  const char *id;
  if (!readIdField(clientId, msg, id)) return;
  StreamCommand command{};
  command.kind = StreamCommandKind::Unsubscribe;
  command.request.clientId = clientId;
  strlcpy(command.request.id, id, sizeof(command.request.id));
  if (!submitStreamCommand(command)) {
    wsSendError(clientId, id, "busy", "Gateway is busy, try again shortly");
  }
}

void handleMessage(AsyncWebSocketClient *client, const uint8_t *data, size_t len) {
  uint32_t clientId = client->id();
  JsonDocument msg;
  if (deserializeJson(msg, data, len) != DeserializationError::Ok || !msg.is<JsonObject>()) {
    wsSendError(clientId, nullptr, "invalid_message", "Message must be a JSON object");
    return;
  }
  const char *op = msg["op"].as<const char *>();
  if (op != nullptr && strcmp(op, "subscribe") == 0) {
    handleSubscribe(clientId, msg.as<JsonVariantConst>());
  } else if (op != nullptr && strcmp(op, "unsubscribe") == 0) {
    handleUnsubscribe(clientId, msg.as<JsonVariantConst>());
  } else {
    wsSendError(clientId, nullptr, "invalid_message", "'op' must be subscribe or unsubscribe");
  }
}

void onEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg,
             uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      if (ws.count() > WS_MAX_CLIENTS) {
        wsSendError(client->id(), nullptr, "too_many_clients", "Connection limit reached");
        client->close();
        return;
      }
      sendHello(client);
      break;
    case WS_EVT_DISCONNECT:
      forgetClient(client->id());
      submitClientGone(client->id());
      break;
    case WS_EVT_DATA: {
      auto *info = static_cast<AwsFrameInfo *>(arg);
      // Only small, unfragmented text messages -- that's all the protocol
      // needs. One such frame may still arrive in several pieces when it
      // spans TCP segments (index > 0 or len < info->len): reassemble it.
      bool singleFrame = info->num == 0 && info->final && info->message_opcode == WS_TEXT;
      if (!singleFrame || info->len > WS_MAX_MESSAGE_BYTES) {
        // Once per message, not once per piece or continuation frame.
        if (info->num == 0 && info->index == 0) {
          wsSendError(client->id(), nullptr, "invalid_message",
                      "Messages must be a single text frame of at most 512 bytes");
        }
        return;
      }
      if (info->index == 0 && info->len == len) {
        handleMessage(client, data, len);
        return;
      }
      String message;
      if (appendFramePiece(client->id(), *info, data, len, message)) {
        handleMessage(client, reinterpret_cast<const uint8_t *>(message.c_str()), message.length());
      }
      break;
    }
    default:
      break;
  }
}

} // namespace

void registerWsStream(AsyncWebServer &server) {
  pendingGoneMutex = xSemaphoreCreateMutex();
  clientStateMutex = xSemaphoreCreateMutex();
  ws.onEvent(onEvent);
  server.addHandler(&ws);
}

void wsMaintenance() {
  ws.cleanupClients(WS_MAX_CLIENTS);

  xSemaphoreTake(pendingGoneMutex, portMAX_DELAY);
  std::vector<uint32_t> retry;
  retry.swap(pendingGone);
  xSemaphoreGive(pendingGoneMutex);
  for (uint32_t clientId : retry) submitClientGone(clientId);
}

size_t wsClientCount() {
  return ws.count();
}

bool wsCanSend(uint32_t clientId) {
  xSemaphoreTake(clientStateMutex, portMAX_DELAY);
  bool queued = pendingRepliesLocked(clientId) > 0;
  xSemaphoreGive(clientStateMutex);
  return !queued && ws.availableForWrite(clientId);
}

void wsFlushReplies() {
  // Sends each client's oldest reply until its queue is full again. The
  // mutex is released around every ws.* call (see the lock order above),
  // so the next reply is looked up again each time. A stalled client is
  // skipped entirely: sending a later reply first would reorder them.
  std::vector<uint32_t> stalled;
  while (true) {
    xSemaphoreTake(clientStateMutex, portMAX_DELAY);
    auto next = std::find_if(outbox.begin(), outbox.end(), [&stalled](const PendingReply &reply) {
      return std::find(stalled.begin(), stalled.end(), reply.clientId) == stalled.end();
    });
    if (next == outbox.end()) {
      xSemaphoreGive(clientStateMutex);
      return;
    }
    uint32_t clientId = next->clientId;
    xSemaphoreGive(clientStateMutex);

    if (!ws.availableForWrite(clientId)) {
      stalled.push_back(clientId);
      continue;
    }

    xSemaphoreTake(clientStateMutex, portMAX_DELAY);
    // Re-find it: the client may have disconnected meanwhile.
    auto reply = std::find_if(outbox.begin(), outbox.end(),
                              [clientId](const PendingReply &r) { return r.clientId == clientId; });
    String text;
    bool found = reply != outbox.end();
    if (found) {
      text = std::move(reply->text);
      outbox.erase(reply);
    }
    xSemaphoreGive(clientStateMutex);
    if (found) ws.text(clientId, text);
  }
}

void wsSendInterval(const Subscription &sub, const char *type) {
  JsonDocument doc;
  doc["type"] = type;
  addSubscriptionFields(doc, sub);
  doc["startAddress"] = sub.start;
  doc["count"] = sub.count;
  doc["intervalMs"] = sub.intervalMs;
  doc["effectiveIntervalMs"] = sub.reportedIntervalMs;
  sendReply(sub.clientId, doc);
}

void wsSendUnsubscribed(uint32_t clientId, const char *id) {
  JsonDocument doc;
  doc["type"] = "unsubscribed";
  doc["id"] = id;
  sendReply(clientId, doc);
}

void wsSendSnapshot(const Subscription &sub, const uint16_t *values, uint32_t readAt) {
  JsonDocument doc;
  doc["type"] = "snapshot";
  addSubscriptionFields(doc, sub);
  doc["startAddress"] = sub.start;
  doc["count"] = sub.count;
  JsonArray list = doc["values"].to<JsonArray>();
  for (uint16_t i = 0; i < sub.count; i++) list.add(values[i]);
  doc["ts"] = readAt;
  sendDoc(sub.clientId, doc);
}

void wsSendUpdate(const Subscription &sub, const std::vector<std::pair<uint16_t, uint16_t>> &changes,
                  uint32_t readAt) {
  JsonDocument doc;
  doc["type"] = "update";
  doc["id"] = sub.id;
  JsonArray list = doc["changes"].to<JsonArray>();
  for (const auto &change : changes) {
    JsonArray pair = list.add<JsonArray>();
    pair.add(change.first);
    pair.add(change.second);
  }
  doc["ts"] = readAt;
  sendDoc(sub.clientId, doc);
}

void wsSendModbusError(const Subscription &sub, uint8_t result, uint32_t retryInMs) {
  ModbusFailure failure = describeModbusFailure(result);
  JsonDocument doc;
  doc["type"] = "error";
  addSubscriptionFields(doc, sub);
  doc["error"] = failure.error;
  doc["message"] = failure.message;
  doc["modbusCode"] = result;
  doc["modbusError"] = modbusResultName(result);
  doc["retryInMs"] = retryInMs;
  sendDoc(sub.clientId, doc);
}

void wsSendError(uint32_t clientId, const char *id, const char *error, const char *message) {
  JsonDocument doc;
  doc["type"] = "error";
  if (id != nullptr) doc["id"] = id;
  doc["error"] = error;
  doc["message"] = message;
  sendReply(clientId, doc);
}
