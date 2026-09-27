#include "route_helpers.h"

#include "config.h"
#include "json_codec.h"

namespace {

// Raw body accumulated across onBody chunks. Allocated with malloc
// because the library releases `_tempObject` with free().
struct BodyBuffer {
  size_t length;
  char data[]; // length bytes + NUL
};

bool isAllDigits(const String &raw) {
  if (raw.length() == 0) return false;
  for (char c : raw) {
    if (!isDigit(c)) return false;
  }
  return true;
}

} // namespace

void registerJsonRoute(AsyncWebServer &server, const String &uri,
                        WebRequestMethodComposite method, JsonBodyHandler handler) {
  server.on(
      uri.c_str(), method,
      [handler](AsyncWebServerRequest *request) {
        auto *body = static_cast<BodyBuffer *>(request->_tempObject);
        JsonDocument doc;
        if (body == nullptr || deserializeJson(doc, body->data, body->length) != DeserializationError::Ok) {
          sendInvalidBody(request);
          return;
        }
        handler(request, doc.as<JsonVariantConst>());
      },
      nullptr,
      [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (index == 0) {
          // A retransmitted first chunk would otherwise leak the old buffer.
          free(request->_tempObject);
          auto *body = static_cast<BodyBuffer *>(malloc(sizeof(BodyBuffer) + total + 1));
          if (body == nullptr) {
            request->_tempObject = nullptr;
            return;
          }
          body->length = total;
          body->data[total] = '\0';
          request->_tempObject = body;
        }
        auto *body = static_cast<BodyBuffer *>(request->_tempObject);
        if (body == nullptr || index + len > body->length) return;
        memcpy(body->data + index, data, len);
      });
}

ModbusFunction functionCodeFor(ModbusTable table, RouteKind kind) {
  switch (kind) {
    case RouteKind::Read:
      return readFunctionFor(table);
    case RouteKind::WriteSingle:
      return table == ModbusTable::Coils ? ModbusFunction::WriteSingleCoil
                                         : ModbusFunction::WriteSingleRegister;
    case RouteKind::WriteMultiple:
      return table == ModbusTable::Coils ? ModbusFunction::WriteMultipleCoils
                                         : ModbusFunction::WriteMultipleRegisters;
  }
  return ModbusFunction::ReadHoldingRegisters; // unreachable
}

bool parseAddressParam(AsyncWebServerRequest *request, const String &raw, uint16_t &address) {
  if (!isAllDigits(raw) || raw.length() > 5 || raw.toInt() > 0xFFFF) {
    sendJsonError(request, 400, "invalid_address", "Address must be an integer from 0 to 65535");
    return false;
  }
  address = static_cast<uint16_t>(raw.toInt());
  return true;
}

bool parseQueryParam(AsyncWebServerRequest *request, const char *name, uint32_t min,
                      uint32_t max, bool required, uint32_t fallback, uint32_t &out) {
  const AsyncWebParameter *param = request->getParam(name);
  if (param == nullptr) {
    if (!required) {
      out = fallback;
      return true;
    }
    String message = String("Query parameter '") + name + "' is required";
    sendJsonError(request, 400, "missing_parameter", message.c_str());
    return false;
  }

  const String &raw = param->value();
  long value = raw.toInt();
  if (!isAllDigits(raw) || raw.length() > 5 || value < static_cast<long>(min) ||
      value > static_cast<long>(max)) {
    String message = String("Query parameter '") + name + "' must be an integer from " +
                     min + " to " + max;
    sendJsonError(request, 400, "invalid_parameter", message.c_str());
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool parseSlaveParam(AsyncWebServerRequest *request, uint8_t &slave) {
  uint32_t value;
  if (!parseQueryParam(request, "slave", MODBUS_MIN_SLAVE_ID, MODBUS_MAX_SLAVE_ID, false,
                       DEFAULT_SLAVE_ID, value)) {
    return false;
  }
  slave = static_cast<uint8_t>(value);
  return true;
}

void dispatchModbusJob(AsyncWebServerRequest *request, std::unique_ptr<ModbusJob> job) {
  // Checked before pausing: only this (async_tcp) task submits, so the
  // space can't disappear before submitModbusJob below.
  if (!modbusQueueHasSpace()) {
    sendJsonError(request, 503, "busy", "Too many pending Modbus requests, try again shortly");
    return;
  }
  job->request = request->pause();
  submitModbusJob(std::move(job));
}

void sendInvalidBody(AsyncWebServerRequest *request) {
  sendJsonError(request, 400, "invalid_body", "Request body is missing or malformed");
}

void sendReadOnly(AsyncWebServerRequest *request) {
  sendJsonError(request, 405, "read_only", "This table cannot be written over Modbus");
}
