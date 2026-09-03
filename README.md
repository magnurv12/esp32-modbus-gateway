# esp32-modbus-gateway

ESP32 firmware acting as a **Modbus RTU master**: it polls servo drives on
an RS-485 network, and forwards the readings to the cloud.

## Hardware

Modbus RTU runs on **UART2** through an RS-485 transceiver module
(MAX485/MAX3485), which keeps **UART0/USB free for debug prints** via
`Serial` and `pio device monitor`.

**Mac side:** USB-RS485 dongle, opened as the serial port in Modbus Server
Pro.

**ESP32 side:** RS-485 module wired as:

| Module pin | Connects to |
|---|---|
| A | A wire from the Mac's USB-RS485 dongle |
| B | B wire from the Mac's USB-RS485 dongle |
| RO | ESP32 GPIO16 (RX2) |
| DI | ESP32 GPIO17 (TX2) |
| DE + RE (tied together) | ESP32 GPIO23 |
| VCC | ESP32 5V (or 3V3 if the module is a MAX3485) |
| GND | ESP32 GND |

If reads keep timing out after wiring, try swapping A/B — labeling isn't
always consistent between manufacturers.

## Build / flash / monitor

```sh
pio run                # build
pio run -t upload      # flash
pio device monitor      # debug output (Serial), independent of the Modbus link
```

## Bench test steps

1. Plug the USB-RS485 dongle into the Mac. In Modbus Server Pro (RTU tab),
   select that port, baud 9600 / 8 / none / 1, Device ID 1.
2. Add a few Holding register rows (addresses 0-7) with test values, click
   Connect.
3. Wire the ESP32-side RS-485 module per the table above, flash this
   firmware, and open the serial monitor.
4. You should see the 8 holding register values printed once per second.

## Status

- [x] Modbus RTU master polling holding registers over RS-485
- [ ] Real servo network (multiple slaves)
- [ ] Cloud publishing (transport/provider TBD)
