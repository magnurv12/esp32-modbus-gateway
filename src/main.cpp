#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include "config.h"
#include "cors.h"
#include "json_codec.h"
#include "modbus_task.h"
#include "route_helpers.h"
#include "routes_docs.h"
#include "routes_health.h"
#include "routes_tables.h"
#include "wifi_setup.h"
#include "ws_stream.h"

AsyncWebServer server(HTTP_PORT);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(DE_RE_PIN, OUTPUT);
  digitalWrite(DE_RE_PIN, LOW); // start in receive mode

  Serial.begin(DEBUG_BAUD);

  connectWifiAndMdns();

  enableCors(server);
  registerTableRoutes(server);
  registerHealthRoutes(server);
  registerDocsRoutes(server);
  registerWsStream(server);
  server.onNotFound([](AsyncWebServerRequest *request) {
    sendJsonError(request, 404, "not_found", "No such route");
  });

  // Owns the RS-485 bus exclusively: runs each API request's Modbus
  // transaction in arrival order and answers the paused HTTP request.
  // HTTP handlers never touch Serial2 directly. Order matters: the task
  // uses the WebSocket state set up by registerWsStream(), and handlers
  // use the task's mutex and queues from the very first request.
  startModbusTask();
  server.begin();
}

void loop() {
  // Everything else happens in modbusTask + async HTTP/WebSocket callbacks.
  wsMaintenance();
  vTaskDelay(pdMS_TO_TICKS(1000));
}
