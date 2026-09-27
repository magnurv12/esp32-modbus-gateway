#include "routes_health.h"

#include <WiFi.h>

#include "config.h"
#include "json_codec.h"
#include "modbus_task.h"
#include "ws_stream.h"

void registerHealthRoutes(AsyncWebServer &server) {
  server.on("/api/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["uptimeMs"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["wifiRssi"] = WiFi.RSSI();
    doc["wifiIp"] = WiFi.localIP().toString();
    doc["defaultSlave"] = DEFAULT_SLAVE_ID;

    ModbusStats stats = getModbusStats();
    JsonObject modbus = doc["modbus"].to<JsonObject>();
    modbus["okCount"] = stats.okCount;
    modbus["errorCount"] = stats.errorCount;
    if (stats.anyAttempt) {
      modbus["lastResult"] = modbusResultName(stats.lastResult);
      modbus["lastSlave"] = stats.lastSlave;
      modbus["lastAttemptAt"] = stats.lastAttemptAt;
    } else {
      modbus["lastResult"] = "NoRequestYet";
      modbus["lastSlave"] = nullptr;
      modbus["lastAttemptAt"] = nullptr;
    }
    if (stats.lastSuccessAt != 0) {
      modbus["lastSuccessAt"] = stats.lastSuccessAt;
    } else {
      modbus["lastSuccessAt"] = nullptr;
    }
    modbus["cacheHits"] = stats.cacheHits;

    JsonObject stream = doc["stream"].to<JsonObject>();
    stream["clients"] = wsClientCount();
    stream["subscriptions"] = stats.subscriptions;
    stream["pollBlocks"] = stats.pollBlocks;
    stream["busLoadPct"] = roundf(stats.busLoad * 1000) / 10; // one decimal
    // A slave answered the latest request -- data or a Modbus exception
    // (0x01-0x04) both prove the RS-485 link works. Without WebSocket
    // subscriptions nothing is polled, so it then reflects the last API call.
    doc["modbusLinkUp"] = stats.anyAttempt && stats.lastResult <= 0x04;

    AsyncResponseStream *response = request->beginResponseStream(JSON_CONTENT_TYPE);
    serializeJson(doc, *response);
    request->send(response);
  });
}
