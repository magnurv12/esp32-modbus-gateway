#include <Arduino.h>
#include <ModbusMaster.h>

// Modbus RTU runs on UART2 through an RS-485 transceiver module, wired to
// the Mac's USB-RS485 dongle (opened by Modbus Server Pro). That frees
// UART0/USB for normal debug prints via `Serial`.
constexpr int MODBUS_RX_PIN = 16;
constexpr int MODBUS_TX_PIN = 17;
constexpr int MODBUS_DE_RE_PIN = 23; // tied DE+RE on the RS-485 module
constexpr uint32_t MODBUS_BAUD = 9600;

constexpr uint8_t SERVO_SLAVE_ID = 1;
constexpr uint16_t HOLDING_REG_START = 0;
constexpr uint16_t HOLDING_REG_COUNT = 8;

HardwareSerial ModbusSerial(2);
ModbusMaster node;

void preTransmission() {
  digitalWrite(MODBUS_DE_RE_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(MODBUS_DE_RE_PIN, LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(MODBUS_DE_RE_PIN, OUTPUT);
  digitalWrite(MODBUS_DE_RE_PIN, LOW);

  ModbusSerial.begin(MODBUS_BAUD, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
  node.begin(SERVO_SLAVE_ID, ModbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("ESP32 Modbus master - RS-485 bench test starting");
}

void loop() {
  uint8_t result = node.readHoldingRegisters(HOLDING_REG_START, HOLDING_REG_COUNT);

  if (result == node.ku8MBSuccess) {
    Serial.printf("Holding registers: ");
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
      Serial.printf("%u ", node.getResponseBuffer(i));
    }
    Serial.println();
  } else {
    Serial.printf("Modbus read failed, error code: 0x%02X\n", result);
  }

  delay(1000);
}
