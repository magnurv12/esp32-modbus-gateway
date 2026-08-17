#include <Arduino.h>
#include <ModbusMaster.h>

// ---- Bench test setup: no RS-485 yet ----
// The S3 DevKitC-1 has two USB-C ports:
//   "USB"  (native CDC, this sketch's `Serial`)  -> cable to the Mac, opened by Modbus Server Pro
//   "UART" (via CP2102, this sketch's `Serial0`) -> cable to the Mac, used only for debug prints
constexpr uint32_t MODBUS_BAUD = 9600;

constexpr uint8_t SERVO_SLAVE_ID = 1;
constexpr uint16_t HOLDING_REG_START = 0;
constexpr uint16_t HOLDING_REG_COUNT = 8;

ModbusMaster node;

void setup() {
  Serial0.begin(115200);
  while (!Serial0) {
    delay(10);
  }

  Serial.begin(MODBUS_BAUD);
  node.begin(SERVO_SLAVE_ID, Serial);

  Serial0.println("ESP32 Modbus master - bench test starting");
}

void loop() {
  uint8_t result = node.readHoldingRegisters(HOLDING_REG_START, HOLDING_REG_COUNT);

  if (result == node.ku8MBSuccess) {
    Serial0.printf("Holding registers: ");
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
      Serial0.printf("%u ", node.getResponseBuffer(i));
    }
    Serial0.println();
  } else {
    Serial0.printf("Modbus read failed, error code: 0x%02X\n", result);
  }

  delay(1000);
}
