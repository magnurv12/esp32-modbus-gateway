# esp32-modbus-gateway

ESP32-S3 firmware acting as a **Modbus RTU master**: it polls servo drives on
an RS-485 network, and forwards the readings to the cloud.

## Hardware

**Current bench test (no RS-485 yet):** the S3 DevKitC-1 has two USB-C ports.
Connect both to the Mac:

- **"USB" port** (native CDC, `Serial` in code) -> open this port in
  Modbus Server Pro, configured as the simulated servo (RTU, 9600 8N1).
- **"UART" port** (via CP2102, `Serial0` in code) -> `pio device monitor`,
  shows the register values the ESP32 reads, purely for debug.

**Later, on the real network:** swap `Serial`/native USB for a
`HardwareSerial` on spare GPIOs wired to an RS-485 transceiver (DE/RE pin
toggled around each transaction), so the native USB port is free again.

## Build / flash / monitor

```sh
pio run                          # build
pio run -t upload                # flash (use the "UART" port)
pio device monitor -p /dev/tty.usbserial-XXXX   # debug output (Serial0)
```

## Bench test steps

1. In Modbus Server Pro (RTU tab), set the Serial Port to the ESP32's
   native "USB" port, baud 9600 / 8 / none / 1, and add a few Holding
   register rows (addresses 0-7) with test values. Click Connect.
2. Flash this firmware and open the serial monitor on the "UART" port.
3. You should see the 8 holding register values printed once per second.

## Status

- [x] Modbus RTU master polling holding registers (bench test over USB)
- [ ] RS-485 wiring for the real servo network
- [ ] Cloud publishing (transport/provider TBD)
