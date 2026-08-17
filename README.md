# esp32-modbus-gateway

ESP32-S3 firmware acting as a **Modbus RTU master**: it polls servo drives on
an RS-485 network, and forwards the readings to the cloud.

## Hardware

- ESP32-S3 DevKitC-1
- RS-485 transceiver module (e.g. MAX485) wired to `UART1`:
  - `RX` -> GPIO17
  - `TX` -> GPIO18
  - `DE/RE` -> GPIO4 (driven high while transmitting, low while receiving)
- Native USB (GPIO19/20) stays free for `Serial` debug output/monitor.

## Build / flash / monitor

```sh
pio run                # build
pio run -t upload      # flash
pio device monitor      # serial monitor (115200 baud)
```

## Status

- [x] Modbus RTU master polling holding registers from a servo slave
- [ ] Cloud publishing (transport/provider TBD)
