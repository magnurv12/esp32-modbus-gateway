#include <Arduino.h>
#include <ModbusMaster.h>

// ---- Modbus RTU serial link (RS-485 to the servo network) ----
constexpr int MODBUS_RX_PIN = 17;
constexpr int MODBUS_TX_PIN = 18;
constexpr int MODBUS_DE_RE_PIN = 4; // RS-485 driver enable; tie module to auto-direction if it has no DE/RE pin
constexpr uint32_t MODBUS_BAUD = 9600;

// ---- Servo polled on the Modbus network ----
constexpr uint8_t SERVO_SLAVE_ID = 1;
constexpr uint16_t HOLDING_REG_START = 0;
constexpr uint16_t HOLDING_REG_COUNT = 8;

HardwareSerial ModbusSerial(1);
ModbusMaster node;

void preTransmission() {
  digitalWrite(MODBUS_DE_RE_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(MODBUS_DE_RE_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  pinMode(MODBUS_DE_RE_PIN, OUTPUT);
  digitalWrite(MODBUS_DE_RE_PIN, LOW);

  ModbusSerial.begin(MODBUS_BAUD, SERIAL_8N1, MODBUS_RX_PIN, MODBUS_TX_PIN);
  node.begin(SERVO_SLAVE_ID, ModbusSerial);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);

  Serial.println("ESP32 Modbus Gateway - master starting");
}

void loop() {
  uint8_t result = node.readHoldingRegisters(HOLDING_REG_START, HOLDING_REG_COUNT);

  if (result == node.ku8MBSuccess) {
    Serial.printf("Servo %u registers: ", SERVO_SLAVE_ID);
    for (uint16_t i = 0; i < HOLDING_REG_COUNT; i++) {
      Serial.printf("%u ", node.getResponseBuffer(i));
    }
    Serial.println();

    // TODO: publish these values to the cloud once the transport (MQTT/HTTP) is decided
  } else {
    Serial.printf("Modbus read failed, error code: 0x%02X\n", result);
  }

  delay(1000);
}
