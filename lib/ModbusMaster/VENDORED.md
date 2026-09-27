# ModbusMaster (vendored)

Copy of [4-20ma/ModbusMaster](https://github.com/4-20ma/ModbusMaster) 2.0.1
(Apache-2.0, see `LICENSE`), kept in `lib/` so PlatformIO builds it instead
of the registry version.

Local change: the response timeout was a fixed `static const` of 2000 ms;
it is now a per-instance value set with `setResponseTimeout(ms)`, so a
dead slave doesn't hold the bus for 2 s during live polling. Search for
`esp32-modbus-gateway` in `src/ModbusMaster.h`.
