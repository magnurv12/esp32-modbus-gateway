#include "json_codec.h"

namespace {

void addCommonFields(JsonDocument &doc, const ModbusJob &job) {
  doc["table"] = tableName(job.table);
  doc["slave"] = job.slave;
  doc["functionCode"] = static_cast<uint8_t>(job.function);
}

void sendJson(AsyncWebServerRequest *request, int status, JsonDocument &doc) {
  AsyncResponseStream *response = request->beginResponseStream(JSON_CONTENT_TYPE);
  response->setCode(status);
  serializeJson(doc, *response);
  request->send(response);
}

// Writes `count` values as a list: registers as [{address, value}], bits
// as a plain [true, false, ...] (index i is startAddress + i).
void addValueList(JsonDocument &doc, ModbusTable table, uint16_t startAddress,
                  const uint16_t *values, uint16_t count) {
  doc["startAddress"] = startAddress;
  doc["count"] = count;
  if (isBitTable(table)) {
    JsonArray states = doc["states"].to<JsonArray>();
    for (uint16_t i = 0; i < count; i++) states.add(values[i] != 0);
  } else {
    JsonArray registers = doc["registers"].to<JsonArray>();
    for (uint16_t i = 0; i < count; i++) {
      JsonObject reg = registers.add<JsonObject>();
      reg["address"] = startAddress + i;
      reg["value"] = values[i];
    }
  }
}

void addSingleValue(JsonDocument &doc, ModbusTable table, uint16_t address, uint16_t value) {
  doc["address"] = address;
  if (isBitTable(table)) {
    doc["state"] = value != 0;
  } else {
    doc["value"] = value;
  }
}

bool parseStartAndArray(JsonVariantConst body, const char *field, uint16_t &startAddress,
                        JsonArrayConst &arr, uint16_t maxCount) {
  if (!body["startAddress"].is<uint16_t>()) return false;
  JsonVariantConst list = body[field];
  if (!list.is<JsonArrayConst>()) return false;
  arr = list.as<JsonArrayConst>();
  if (arr.size() == 0 || arr.size() > maxCount) return false;
  // The whole range must fit in the 16-bit address space.
  startAddress = body["startAddress"].as<uint16_t>();
  return static_cast<uint32_t>(startAddress) + arr.size() <= 0x10000;
}

} // namespace

void sendReadResult(AsyncWebServerRequest *request, const ModbusJob &job,
                     const uint16_t *values) {
  JsonDocument doc;
  addCommonFields(doc, job);
  if (job.singleAddressRoute) {
    addSingleValue(doc, job.table, job.address, values[0]);
  } else {
    addValueList(doc, job.table, job.address, values, job.count);
  }
  // Answered from a block the WebSocket stream polls, not a fresh bus read.
  if (job.fromCache) {
    doc["cached"] = true;
    doc["ageMs"] = job.cacheAgeMs;
  }
  sendJson(request, 200, doc);
}

void sendWriteResult(AsyncWebServerRequest *request, const ModbusJob &job) {
  JsonDocument doc;
  addCommonFields(doc, job);
  if (job.singleAddressRoute) {
    addSingleValue(doc, job.table, job.address, job.values[0]);
  } else {
    addValueList(doc, job.table, job.address, job.values.data(), job.count);
  }
  doc["status"] = "written";
  sendJson(request, 200, doc);
}

ModbusFailure describeModbusFailure(uint8_t result) {
  // Exceptions (0x01-0x04) mean the slave answered and refused; the rest
  // are link-level failures where nothing usable came back.
  switch (result) {
    case 0x01:
      return {400, "illegal_function", "The slave does not support this Modbus function"};
    case 0x02:
      return {404, "illegal_address", "The slave has no data at (part of) this address range"};
    case 0x03:
      return {400, "illegal_value", "The slave rejected the quantity or value"};
    case 0x04:
      return {502, "slave_failure", "The slave failed while handling the request"};
    case MODBUS_RESULT_TIMEOUT:
      return {504, "slave_timeout",
              "The Modbus slave did not respond (check RS-485 wiring, slave id and baud rate)"};
    default:
      return {502, "bad_response", "The Modbus slave sent an invalid response"};
  }
}

void sendModbusFailure(AsyncWebServerRequest *request, const ModbusJob &job,
                        uint8_t result) {
  ModbusFailure failure = describeModbusFailure(result);
  JsonDocument doc;
  doc["error"] = failure.error;
  doc["message"] = failure.message;
  addCommonFields(doc, job);
  doc["address"] = job.address;
  doc["count"] = job.count;
  doc["modbusCode"] = result;
  doc["modbusError"] = modbusResultName(result);
  sendJson(request, failure.httpStatus, doc);
}

void sendJsonError(AsyncWebServerRequest *request, int status,
                    const char *error, const char *message) {
  JsonDocument doc;
  doc["error"] = error;
  doc["message"] = message;
  sendJson(request, status, doc);
}

bool parseValueField(JsonVariantConst body, uint16_t &value) {
  if (!body["value"].is<uint16_t>()) return false;
  value = body["value"].as<uint16_t>();
  return true;
}

bool parseStateField(JsonVariantConst body, bool &state) {
  if (!body["state"].is<bool>()) return false;
  state = body["state"].as<bool>();
  return true;
}

bool parseValuesField(JsonVariantConst body, uint16_t &startAddress,
                       std::vector<uint16_t> &out, uint16_t maxCount) {
  JsonArrayConst arr;
  if (!parseStartAndArray(body, "values", startAddress, arr, maxCount)) return false;
  out.clear();
  for (JsonVariantConst v : arr) {
    if (!v.is<uint16_t>()) return false;
    out.push_back(v.as<uint16_t>());
  }
  return true;
}

bool parseStatesField(JsonVariantConst body, uint16_t &startAddress,
                       std::vector<uint16_t> &out, uint16_t maxCount) {
  JsonArrayConst arr;
  if (!parseStartAndArray(body, "states", startAddress, arr, maxCount)) return false;
  out.clear();
  for (JsonVariantConst v : arr) {
    if (!v.is<bool>()) return false;
    out.push_back(v.as<bool>() ? 1 : 0);
  }
  return true;
}
