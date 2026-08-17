# esp32-modbus-gateway

ESP32 firmware acting as a **Modbus RTU master**: it polls servo drives on
an RS-485 network, and forwards the readings to the cloud.

## Hardware

**Current bench test (no RS-485 yet):** a classic ESP32 DevKit has a single
serial link — UART0, over the onboard USB-serial chip. That link stays
dedicated to Modbus RTU traffic with Modbus Server Pro on the Mac, so there
is no room on the wire for debug prints at the same time:

- Onboard **LED (GPIO2)** blinks once (150ms) on a successful read, or three
  times fast (80ms) on failure/timeout.
- The actual register values are visible in Modbus Server Pro's own
  **Data Log** window (request/response bytes) and in the Holding table
  you configured.

**Later, on the real network:** move Modbus off UART0 onto a spare
`HardwareSerial` (e.g. UART2, GPIO16/17) wired to an RS-485 transceiver
(DE/RE pin toggled around each transaction). That frees UART0/USB for
normal debug output again.

## Build / flash / monitor

```sh
pio run                # build
pio run -t upload      # flash
pio device monitor      # only useful before/after the Modbus test,
                         # since UART0 is occupied by Modbus while polling
```

## Bench test steps

1. In Modbus Server Pro (RTU tab), set the Serial Port to the ESP32's
   USB-serial port, baud 9600 / 8 / none / 1, and add a few Holding
   register rows (addresses 0-7) with test values. Click Connect.
2. Flash this firmware.
3. Watch the onboard LED: one blink per second = successful read; three
   fast blinks = failure. Check the Data Log window in the app to see the
   actual bytes/values being exchanged.

## Status

- [x] Modbus RTU master polling holding registers (bench test over USB)
- [ ] RS-485 wiring for the real servo network
- [ ] Cloud publishing (transport/provider TBD)
