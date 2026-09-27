#pragma once

#include <stdint.h>
#include <string.h>

#include <initializer_list>

enum class ModbusTable {
  Holding,
  Input,
  Coils,
  Discrete,
};

inline const char *tableName(ModbusTable table) {
  switch (table) {
    case ModbusTable::Holding: return "holding";
    case ModbusTable::Input: return "input";
    case ModbusTable::Coils: return "coils";
    case ModbusTable::Discrete: return "discrete";
  }
  return "unknown";
}

// Inverse of tableName(); false for anything else.
inline bool parseTableName(const char *name, ModbusTable &table) {
  if (name == nullptr) return false;
  for (ModbusTable candidate : {ModbusTable::Holding, ModbusTable::Input,
                                ModbusTable::Coils, ModbusTable::Discrete}) {
    if (strcmp(name, tableName(candidate)) == 0) {
      table = candidate;
      return true;
    }
  }
  return false;
}

inline bool isBitTable(ModbusTable table) {
  return table == ModbusTable::Coils || table == ModbusTable::Discrete;
}

inline bool isWritableTable(ModbusTable table) {
  return table == ModbusTable::Holding || table == ModbusTable::Coils;
}

// Per-request limits. Reads follow the Modbus spec (FC 01-04); modbusTask
// splits them into ModbusMaster-sized chunks. Writes are capped by
// ModbusMaster's 64-word transmit buffer and are never split, so a block
// write stays a single FC 15/16 transaction.
constexpr uint16_t MODBUS_MAX_READ_REGISTERS = 125;
constexpr uint16_t MODBUS_MAX_READ_BITS = 2000;
constexpr uint16_t MODBUS_MAX_WRITE_VALUES = 64;

constexpr uint8_t MODBUS_MIN_SLAVE_ID = 1;
constexpr uint8_t MODBUS_MAX_SLAVE_ID = 247;

// ModbusMaster result codes: 0x01-0x04 are genuine protocol exceptions
// the slave sent back; 0xE0-0xE3 are local/link-level failures (nothing
// usable came back at all).
constexpr uint8_t MODBUS_RESULT_OK = 0x00;
constexpr uint8_t MODBUS_RESULT_TIMEOUT = 0xE2;

inline const char *modbusResultName(uint8_t code) {
  switch (code) {
    case 0x00: return "Success";
    case 0x01: return "IllegalFunction";
    case 0x02: return "IllegalDataAddress";
    case 0x03: return "IllegalDataValue";
    case 0x04: return "SlaveDeviceFailure";
    case 0xE0: return "InvalidSlaveID";
    case 0xE1: return "InvalidFunction";
    case 0xE2: return "ResponseTimedOut";
    case 0xE3: return "InvalidCRC";
    default: return "Unknown";
  }
}
