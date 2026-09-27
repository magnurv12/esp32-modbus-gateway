#pragma once

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

#include <memory>
#include <vector>

#include "modbus_types.h"
#include "poll_scheduler.h"

// Modbus function codes the gateway issues. Each HTTP route maps to
// exactly one of them (see functionCodeFor in route_helpers.cpp).
enum class ModbusFunction : uint8_t {
  ReadCoils = 0x01,
  ReadDiscreteInputs = 0x02,
  ReadHoldingRegisters = 0x03,
  ReadInputRegisters = 0x04,
  WriteSingleCoil = 0x05,
  WriteSingleRegister = 0x06,
  WriteMultipleCoils = 0x0F,
  WriteMultipleRegisters = 0x10,
};

inline bool isWriteFunction(ModbusFunction fn) {
  return static_cast<uint8_t>(fn) >= 0x05;
}

inline ModbusFunction readFunctionFor(ModbusTable table) {
  switch (table) {
    case ModbusTable::Coils: return ModbusFunction::ReadCoils;
    case ModbusTable::Discrete: return ModbusFunction::ReadDiscreteInputs;
    case ModbusTable::Input: return ModbusFunction::ReadInputRegisters;
    case ModbusTable::Holding: break;
  }
  return ModbusFunction::ReadHoldingRegisters;
}

// One HTTP request's worth of Modbus work. The route handler pauses the
// HTTP request, fills this in and hands it to modbusTask, which runs the
// transaction and answers the (still paused) request itself.
struct ModbusJob {
  ModbusFunction function;
  ModbusTable table;
  uint8_t slave;
  uint16_t address;              // first address
  uint16_t count;                // number of registers/bits
  bool singleAddressRoute;       // /api/<table>/{address}: answer with one value, not a list
  std::vector<uint16_t> values;  // write payload (0/1 per coil)
  AsyncWebServerRequestPtr request;
  // Set by modbusTask when a read was answered from a live-polled block
  // instead of the bus.
  bool fromCache = false;
  uint32_t cacheAgeMs = 0;
};

// Starts the FreeRTOS task that owns the RS-485 bus. Each loop it runs
// queued API jobs first (in arrival order), then the most overdue
// live-polling block (see poll_scheduler.h).
void startModbusTask();

// True when submitModbusJob will accept a job. Only the async_tcp task
// submits, so a true answer can't go stale before the submit.
bool modbusQueueHasSpace();

// Takes ownership of the job. Returns false (job discarded) if the queue
// is full.
bool submitModbusJob(std::unique_ptr<ModbusJob> job);

// WebSocket subscription changes, forwarded to modbusTask (which owns the
// PollScheduler). Only `request.clientId`/`request.id` are used for
// Unsubscribe and ClientGone.
enum class StreamCommandKind : uint8_t { Subscribe, Unsubscribe, ClientGone };
struct StreamCommand {
  StreamCommandKind kind;
  SubscribeRequest request;
};
// Returns false if the command queue is full.
bool submitStreamCommand(const StreamCommand &command);

// Outcome of the most recent bus transactions, for /api/health.
struct ModbusStats {
  uint32_t okCount = 0;
  uint32_t errorCount = 0;
  bool anyAttempt = false;
  uint8_t lastResult = MODBUS_RESULT_OK;
  uint8_t lastSlave = 0;
  uint32_t lastAttemptAt = 0;  // millis()
  uint32_t lastSuccessAt = 0;  // millis(), 0 = never
  uint32_t cacheHits = 0;      // REST reads answered from live-polled blocks
  // Live polling (WebSocket subscriptions)
  uint16_t subscriptions = 0;
  uint16_t pollBlocks = 0;
  float busLoad = 0;           // estimated share of bus time, 0..1
};
ModbusStats getModbusStats();
