#include <Arduino.h>
#include <ModbusMaster.h>

// Bench test: this classic ESP32 has a single serial link (UART0, over the
// onboard USB-serial chip), which stays dedicated to Modbus RTU traffic
// with Modbus Server Pro. No debug prints share that wire -- use the
// onboard LED for pass/fail feedback, and the app's Data Log window to
// see the actual request/response bytes and register values.
constexpr uint32_t MODBUS_BAUD = 9600;
constexpr uint8_t SERVO_SLAVE_ID = 1;
constexpr uint16_t HOLDING_REG_START = 0;
constexpr uint16_t HOLDING_REG_COUNT = 8;
constexpr int LED_PIN = 2; // onboard LED on most ESP32 DevKit boards

ModbusMaster node;

void blink(int times, int ms) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
    digitalWrite(LED_PIN, LOW);
    delay(ms);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(MODBUS_BAUD, SERIAL_8N1);
  node.begin(SERVO_SLAVE_ID, Serial);
}

void loop() {
  uint8_t result = node.readHoldingRegisters(HOLDING_REG_START, HOLDING_REG_COUNT);

  if (result == node.ku8MBSuccess) {
    blink(1, 150);
  } else {
    blink(3, 80);
  }

  delay(700);
}
