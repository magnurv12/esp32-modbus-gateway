#include "modbus_task.h"

#include <ModbusMaster.h>

#include "config.h"
#include "json_codec.h"
#include "poll_scheduler.h"

namespace {

// ModbusMaster's response/transmit buffers hold 64 words, so one read
// transaction carries at most 64 registers or 64 * 16 bits.
constexpr uint16_t kChunkRegisters = 64;
constexpr uint16_t kChunkBits = 64 * 16;
// Silence between consecutive transactions (> 3.5 chars at 9600 baud).
constexpr uint32_t kInterFrameGapMs = 5;
// Longest the task sleeps without checking for new subscription commands.
constexpr uint32_t kCommandPollMs = 20;
// REST reads may be answered from a live-polled block at most this old.
constexpr uint32_t kCacheMaxAgeMs = 1000;

ModbusMaster node;
QueueHandle_t jobQueue = nullptr;
QueueHandle_t commandQueue = nullptr;
PollScheduler scheduler;

SemaphoreHandle_t statsMutex = nullptr;
ModbusStats stats;

bool ledOn = false;
uint32_t ledOffAt = 0;

void preTransmission() {
  digitalWrite(DE_RE_PIN, HIGH); // driver enabled: transmit
}

void postTransmission() {
  digitalWrite(DE_RE_PIN, LOW); // back to listening
}

// Short LED blink on every successful transaction (read or write):
// blinking means Modbus traffic is really flowing, dark means requests go
// unanswered. Non-blocking -- updateLed() switches it off -- so the blink
// never costs bus time.
void flashActivityLed() {
  digitalWrite(LED_PIN, HIGH);
  ledOn = true;
  ledOffAt = millis() + LED_FLASH_MS;
}

void updateLed() {
  if (ledOn && static_cast<int32_t>(millis() - ledOffAt) >= 0) {
    digitalWrite(LED_PIN, LOW);
    ledOn = false;
  }
}

void recordResult(uint8_t slave, uint8_t result) {
  xSemaphoreTake(statsMutex, portMAX_DELAY);
  uint32_t now = millis();
  stats.anyAttempt = true;
  stats.lastResult = result;
  stats.lastSlave = slave;
  stats.lastAttemptAt = now;
  if (result == MODBUS_RESULT_OK) {
    stats.okCount++;
    stats.lastSuccessAt = now;
  } else {
    stats.errorCount++;
  }
  xSemaphoreGive(statsMutex);
}

void publishSchedulerStats() {
  xSemaphoreTake(statsMutex, portMAX_DELAY);
  stats.subscriptions = scheduler.subscriptionCount();
  stats.pollBlocks = scheduler.blockCount();
  stats.busLoad = scheduler.busLoad();
  xSemaphoreGive(statsMutex);
}

uint8_t readChunk(ModbusFunction function, uint16_t address, uint16_t quantity) {
  switch (function) {
    case ModbusFunction::ReadCoils: return node.readCoils(address, quantity);
    case ModbusFunction::ReadDiscreteInputs: return node.readDiscreteInputs(address, quantity);
    case ModbusFunction::ReadHoldingRegisters: return node.readHoldingRegisters(address, quantity);
    case ModbusFunction::ReadInputRegisters: return node.readInputRegisters(address, quantity);
    default: return node.ku8MBInvalidFunction; // callers never pass this
  }
}

// Reads `count` registers/bits of `table` into `out` (one entry per
// address, bits as 0/1), splitting into as many transactions as
// ModbusMaster needs.
uint8_t executeRead(uint8_t slave, ModbusTable table, uint16_t address, uint16_t count,
                    uint16_t *out) {
  node.begin(slave, Serial2);
  ModbusFunction function = readFunctionFor(table);
  bool bits = isBitTable(table);
  uint16_t chunk = bits ? kChunkBits : kChunkRegisters;

  for (uint32_t offset = 0; offset < count; offset += chunk) {
    if (offset > 0) vTaskDelay(pdMS_TO_TICKS(kInterFrameGapMs));
    uint16_t quantity = min<uint32_t>(chunk, count - offset);
    uint8_t result = readChunk(function, address + offset, quantity);
    if (result != node.ku8MBSuccess) return result;

    for (uint16_t i = 0; i < quantity; i++) {
      out[offset + i] = bits ? (node.getResponseBuffer(i / 16) >> (i % 16)) & 0x01
                             : node.getResponseBuffer(i);
    }
  }
  return node.ku8MBSuccess;
}

uint8_t executeWrite(const ModbusJob &job) {
  node.begin(job.slave, Serial2);
  switch (job.function) {
    case ModbusFunction::WriteSingleRegister:
      return node.writeSingleRegister(job.address, job.values[0]);
    case ModbusFunction::WriteSingleCoil:
      return node.writeSingleCoil(job.address, job.values[0] != 0);
    case ModbusFunction::WriteMultipleRegisters:
      for (uint16_t i = 0; i < job.count; i++) node.setTransmitBuffer(i, job.values[i]);
      return node.writeMultipleRegisters(job.address, job.count);
    case ModbusFunction::WriteMultipleCoils: {
      // writeMultipleCoils reads packed 16-bit words from the transmit
      // buffer, one bit per coil -- pack the 0/1 values into words.
      uint16_t words = (job.count + 15) / 16;
      for (uint16_t w = 0; w < words; w++) {
        uint16_t packed = 0;
        for (uint16_t b = 0; b < 16 && w * 16 + b < job.count; b++) {
          if (job.values[w * 16 + b]) packed |= (1 << b);
        }
        node.setTransmitBuffer(w, packed);
      }
      return node.writeMultipleCoils(job.address, job.count);
    }
    default:
      return node.ku8MBInvalidFunction; // routes never build this
  }
}

void logFailure(uint8_t result, ModbusFunction function, uint8_t slave, ModbusTable table,
                uint16_t address, uint16_t count, const char *origin) {
  Serial.printf("Modbus ERR 0x%02X (%s): %s fc=%u slave=%u table=%s addr=%u count=%u\n",
                 result, modbusResultName(result), origin, static_cast<uint8_t>(function), slave,
                 tableName(table), address, count);
}

// An API request (REST GET/PUT).
void runJob(ModbusJob &job) {
  // Client gave up (disconnected) while queued: don't touch the bus.
  if (job.request.expired()) return;

  std::vector<uint16_t> readValues;
  uint8_t result;
  if (isWriteFunction(job.function)) {
    result = executeWrite(job);
    if (result == MODBUS_RESULT_OK) {
      scheduler.invalidate(job.slave, job.table, job.address, job.count, millis());
    }
  } else {
    readValues.resize(job.count);
    // A block the WebSocket stream polls anyway may already hold the answer.
    if (scheduler.readCached(job.slave, job.table, job.address, job.count, millis(),
                             kCacheMaxAgeMs, readValues.data(), job.cacheAgeMs)) {
      job.fromCache = true;
      result = MODBUS_RESULT_OK;
      xSemaphoreTake(statsMutex, portMAX_DELAY);
      stats.cacheHits++;
      xSemaphoreGive(statsMutex);
    } else {
      result = executeRead(job.slave, job.table, job.address, job.count, readValues.data());
    }
  }

  if (!job.fromCache) {
    recordResult(job.slave, result);
    if (result == MODBUS_RESULT_OK) {
      flashActivityLed();
    } else {
      logFailure(result, job.function, job.slave, job.table, job.address, job.count, "api");
    }
  }

  auto request = job.request.lock();
  if (!request) return;
  if (result != MODBUS_RESULT_OK) {
    sendModbusFailure(request.get(), job, result);
  } else if (isWriteFunction(job.function)) {
    sendWriteResult(request.get(), job);
  } else {
    sendReadResult(request.get(), job, readValues.data());
  }
}

// A live-polling block (WebSocket subscriptions).
void pollBlock(int index) {
  const PollBlock &block = scheduler.block(index);
  std::vector<uint16_t> values(block.count);
  uint8_t result = executeRead(block.slave, block.table, block.start, block.count, values.data());

  recordResult(block.slave, result);
  if (result == MODBUS_RESULT_OK) {
    flashActivityLed();
  } else {
    logFailure(result, readFunctionFor(block.table), block.slave, block.table, block.start,
               block.count, "poll");
  }
  scheduler.onBlockRead(index, result, result == MODBUS_RESULT_OK ? values.data() : nullptr,
                        millis());
}

void drainCommands() {
  StreamCommand command;
  bool changed = false;
  while (xQueueReceive(commandQueue, &command, 0) == pdTRUE) {
    uint32_t now = millis();
    switch (command.kind) {
      case StreamCommandKind::Subscribe:
        scheduler.subscribe(command.request, now);
        break;
      case StreamCommandKind::Unsubscribe:
        scheduler.unsubscribe(command.request.clientId, command.request.id, now);
        break;
      case StreamCommandKind::ClientGone:
        scheduler.removeClient(command.request.clientId, now);
        break;
    }
    changed = true;
  }
  if (changed) publishSchedulerStats();
}

void modbusTaskFn(void *) {
  Serial2.begin(MODBUS_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  node.setResponseTimeout(MODBUS_RESPONSE_TIMEOUT_MS);

  while (true) {
    drainCommands();
    updateLed();

    // Sleep until an API job arrives, the next block is due, or it's time
    // to look at the command queue again -- whichever comes first.
    uint32_t waitMs;
    int due = scheduler.dueBlock(millis(), waitMs);
    if (due < 0) waitMs = min(waitMs, kCommandPollMs);
    else waitMs = 0;

    // API jobs always go first; polling only runs when none is waiting.
    ModbusJob *raw = nullptr;
    if (xQueueReceive(jobQueue, &raw, pdMS_TO_TICKS(waitMs)) == pdTRUE) {
      std::unique_ptr<ModbusJob> job(raw);
      runJob(*job);
      vTaskDelay(pdMS_TO_TICKS(kInterFrameGapMs));
      continue;
    }

    due = scheduler.dueBlock(millis(), waitMs);
    if (due >= 0) {
      pollBlock(due);
      vTaskDelay(pdMS_TO_TICKS(kInterFrameGapMs));
    }
  }
}

} // namespace

void startModbusTask() {
  statsMutex = xSemaphoreCreateMutex();
  jobQueue = xQueueCreate(8, sizeof(ModbusJob *));
  commandQueue = xQueueCreate(16, sizeof(StreamCommand));
  // 12 KB: the task also builds the JSON for REST responses and WebSocket
  // messages (up to 2000 bits).
  xTaskCreatePinnedToCore(modbusTaskFn, "modbus", 12288, nullptr, 2, nullptr, 1);
}

bool modbusQueueHasSpace() {
  return jobQueue != nullptr && uxQueueSpacesAvailable(jobQueue) > 0;
}

bool submitModbusJob(std::unique_ptr<ModbusJob> job) {
  if (jobQueue == nullptr) return false;
  ModbusJob *raw = job.get();
  if (xQueueSend(jobQueue, &raw, 0) != pdTRUE) return false;
  job.release(); // modbusTask owns it now
  return true;
}

bool submitStreamCommand(const StreamCommand &command) {
  return commandQueue != nullptr && xQueueSend(commandQueue, &command, 0) == pdTRUE;
}

ModbusStats getModbusStats() {
  xSemaphoreTake(statsMutex, portMAX_DELAY);
  ModbusStats copy = stats;
  xSemaphoreGive(statsMutex);
  return copy;
}
