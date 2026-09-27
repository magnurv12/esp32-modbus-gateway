#pragma once

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include <vector>

#include "modbus_task.h"
#include "modbus_types.h"

// ---- Responses ----

// Content-Type of every API response, errors included.
constexpr const char *JSON_CONTENT_TYPE = "application/json; charset=utf-8";

// 200 with the values read by `job` (one entry per address, bits as 0/1).
// Single-address routes get {address, value|state}; block routes get a
// list.
void sendReadResult(AsyncWebServerRequest *request, const ModbusJob &job,
                     const uint16_t *values);

// 200 once the slave acknowledged the write.
void sendWriteResult(AsyncWebServerRequest *request, const ModbusJob &job);

// How a failed ModbusMaster result is reported, shared by the REST API
// and the WebSocket stream so both use the same error codes.
struct ModbusFailure {
  int httpStatus;
  const char *error;
  const char *message;
};
ModbusFailure describeModbusFailure(uint8_t result);

// The transaction failed: maps the ModbusMaster result to an HTTP status
// (see describeModbusFailure) and includes the Modbus code.
void sendModbusFailure(AsyncWebServerRequest *request, const ModbusJob &job,
                        uint8_t result);

// Standard error envelope: {"error": code, "message": message}.
void sendJsonError(AsyncWebServerRequest *request, int status,
                    const char *error, const char *message);

// ---- Request body parsing ----
// All return false when the body is malformed; the caller is expected to
// respond 400 via sendJsonError.

bool parseValueField(JsonVariantConst body, uint16_t &value);
bool parseStateField(JsonVariantConst body, bool &state);

// values/states share the same uint16_t representation (0/1 for bits) so
// both flow into ModbusJob::values. At most `maxCount` entries.
bool parseValuesField(JsonVariantConst body, uint16_t &startAddress,
                       std::vector<uint16_t> &out, uint16_t maxCount);
bool parseStatesField(JsonVariantConst body, uint16_t &startAddress,
                       std::vector<uint16_t> &out, uint16_t maxCount);
