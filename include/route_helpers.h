#pragma once

#include <functional>
#include <memory>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "modbus_task.h"
#include "modbus_types.h"

// Registers a route whose body is a JSON object. Handles the chunked
// onBody accumulation (see ESPAsyncWebServer's `_tempObject` idiom) and
// only invokes `handler` once the body parsed successfully -- otherwise
// responds 400 itself.
using JsonBodyHandler = std::function<void(AsyncWebServerRequest *, JsonVariantConst)>;
void registerJsonRoute(AsyncWebServer &server, const String &uri,
                        WebRequestMethodComposite method, JsonBodyHandler handler);

// The one Modbus function each kind of route issues:
//   GET  /api/<table>, /api/<table>/{address} -> 01/02/03/04 (reads are
//        always block reads; one address is a block of 1)
//   PUT  /api/<table>/{address}               -> 05 (coil) / 06 (register)
//   PUT  /api/<table>                         -> 15 (coils) / 16 (registers),
//        even for a single value
enum class RouteKind { Read, WriteSingle, WriteMultiple };
ModbusFunction functionCodeFor(ModbusTable table, RouteKind kind);

// Parses the captured path segment (request->pathArg(0)); returns false
// (and has already responded 400) if it isn't a valid uint16.
bool parseAddressParam(AsyncWebServerRequest *request, const String &raw, uint16_t &address);

// Parses an unsigned integer query parameter in [min, max]. When absent,
// `out` = `fallback` if `required` is false, else responds 400. Returns
// false (and has already responded 400) on a missing/invalid value.
bool parseQueryParam(AsyncWebServerRequest *request, const char *name, uint32_t min,
                      uint32_t max, bool required, uint32_t fallback, uint32_t &out);

// `?slave=` (1-247), defaulting to DEFAULT_SLAVE_ID.
bool parseSlaveParam(AsyncWebServerRequest *request, uint8_t &slave);

// Pauses the HTTP request and queues the job; modbusTask answers it once
// the transaction is done. Responds 503 itself if the queue is full.
void dispatchModbusJob(AsyncWebServerRequest *request, std::unique_ptr<ModbusJob> job);

void sendInvalidBody(AsyncWebServerRequest *request);
void sendReadOnly(AsyncWebServerRequest *request);
