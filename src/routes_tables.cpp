#include "routes_tables.h"

#include "json_codec.h"
#include "route_helpers.h"

namespace {

std::unique_ptr<ModbusJob> newJob(ModbusTable table, RouteKind kind, uint8_t slave,
                                  uint16_t address, uint16_t count, bool singleAddressRoute) {
  std::unique_ptr<ModbusJob> job(new ModbusJob());
  job->function = functionCodeFor(table, kind);
  job->table = table;
  job->slave = slave;
  job->address = address;
  job->count = count;
  job->singleAddressRoute = singleAddressRoute;
  return job;
}

void registerRoutesFor(AsyncWebServer &server, ModbusTable table) {
  // Anchored regex, not the "{}" shorthand: a plain "/api/holding" route
  // matches "/api/holding/2" too (prefix match takes priority over the
  // "{}" route regardless of registration order), so the block route
  // must be anchored with "$" to stay exact.
  String name = tableName(table);
  String blockUri = "^\\/api\\/" + name + "$";
  String addressUri = "^\\/api\\/" + name + "\\/([0-9]+)$";
  uint16_t maxRead = maxReadCount(table);

  server.on(blockUri.c_str(), HTTP_GET, [table, maxRead](AsyncWebServerRequest *request) {
    uint8_t slave;
    uint32_t start, count;
    if (!parseSlaveParam(request, slave)) return;
    if (!parseQueryParam(request, "start", 0, 0xFFFF, true, 0, start)) return;
    if (!parseQueryParam(request, "count", 1, maxRead, true, 0, count)) return;
    if (start + count > 0x10000) {
      sendJsonError(request, 400, "invalid_parameter", "start + count must not exceed 65536");
      return;
    }
    dispatchModbusJob(request, newJob(table, RouteKind::Read, slave, start, count, false));
  });

  server.on(addressUri.c_str(), HTTP_GET, [table](AsyncWebServerRequest *request) {
    uint16_t address;
    uint8_t slave;
    if (!parseAddressParam(request, request->pathArg(0), address)) return;
    if (!parseSlaveParam(request, slave)) return;
    dispatchModbusJob(request, newJob(table, RouteKind::Read, slave, address, 1, true));
  });

  if (!isWritableTable(table)) {
    server.on(blockUri.c_str(), HTTP_PUT, [](AsyncWebServerRequest *request) { sendReadOnly(request); });
    server.on(addressUri.c_str(), HTTP_PUT, [](AsyncWebServerRequest *request) { sendReadOnly(request); });
    return;
  }

  registerJsonRoute(server, addressUri, HTTP_PUT, [table](AsyncWebServerRequest *request, JsonVariantConst body) {
    uint16_t address;
    uint8_t slave;
    if (!parseAddressParam(request, request->pathArg(0), address)) return;
    if (!parseSlaveParam(request, slave)) return;

    uint16_t value;
    bool state;
    bool bodyOk = isBitTable(table) ? parseStateField(body, state) : parseValueField(body, value);
    if (!bodyOk) {
      sendInvalidBody(request);
      return;
    }
    auto job = newJob(table, RouteKind::WriteSingle, slave, address, 1, true);
    job->values.push_back(isBitTable(table) ? (state ? 1 : 0) : value);
    dispatchModbusJob(request, std::move(job));
  });

  registerJsonRoute(server, blockUri, HTTP_PUT, [table](AsyncWebServerRequest *request, JsonVariantConst body) {
    uint8_t slave;
    if (!parseSlaveParam(request, slave)) return;

    uint16_t startAddress;
    std::vector<uint16_t> values;
    bool bodyOk = isBitTable(table)
                      ? parseStatesField(body, startAddress, values, MODBUS_MAX_WRITE_VALUES)
                      : parseValuesField(body, startAddress, values, MODBUS_MAX_WRITE_VALUES);
    if (!bodyOk) {
      sendInvalidBody(request);
      return;
    }
    auto job = newJob(table, RouteKind::WriteMultiple, slave, startAddress, values.size(), false);
    job->values = std::move(values);
    dispatchModbusJob(request, std::move(job));
  });
}

} // namespace

void registerTableRoutes(AsyncWebServer &server) {
  for (ModbusTable table : {ModbusTable::Holding, ModbusTable::Input,
                            ModbusTable::Coils, ModbusTable::Discrete}) {
    registerRoutesFor(server, table);
  }
}
