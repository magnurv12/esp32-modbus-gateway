# Live data over WebSocket

`ws://modbus-gateway.local/ws` streams Modbus values as they change. A
client **subscribes** to ranges (slave + table + addresses + interval); the
gateway polls them on the RS-485 bus and pushes a full **snapshot** first,
then only the addresses that **changed**.

Writes and one-off reads stay on the REST API (`PUT`/`GET /api/...`, see
`/docs`) and run alongside the stream: API requests always go before
polling on the bus. A successful `PUT` triggers an immediate re-read of
every subscription covering that address, so all connected clients see the
new value within one transaction (~50 ms at 9600 baud).

Try it on the device: **`http://modbus-gateway.local/ws-test`**.

## Messages

All messages are JSON text frames (client messages: one frame, ≤ 512 bytes).

### Client → gateway

```json
{"op":"subscribe","id":"motor1","slave":1,"table":"holding","start":1,"count":7,"intervalMs":200}
{"op":"unsubscribe","id":"motor1"}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Your name for the subscription (1-32 chars), unique per connection. Subscribing again with the same `id` replaces it. Every message about it carries this `id`. |
| `table` | yes | `holding`, `input`, `coils` or `discrete` |
| `start` | yes | First address (0-65535) |
| `count` | yes | Registers: 1-125. Bits: 1-2000. `start + count` ≤ 65536. |
| `slave` | no | 1-247, default from `config.h` (`hello.defaultSlave`) |
| `intervalMs` | no | How often to poll, 100-60000, default 500 |

### Gateway → client

| `type` | When | Fields |
|---|---|---|
| `hello` | On connect | `clientId`, `defaultSlave`, `limits` |
| `subscribed` | Subscription accepted | `id`, `table`, `slave`, `startAddress`, `count`, `intervalMs`, `effectiveIntervalMs` |
| `snapshot` | First read, and after any error or skipped update | `id`, `table`, `slave`, `startAddress`, `count`, `values`, `ts` |
| `update` | Values changed | `id`, `changes` as `[[address, value], ...]`, `ts` |
| `interval` | The bus budget changed the effective interval | same as `subscribed` |
| `error` | Poll failed, or the request was rejected | `id` (if any), `error`, `message`; for bus errors also `modbusCode`, `modbusError`, `retryInMs` |
| `unsubscribed` | After `unsubscribe` | `id` |

`values` / `changes` hold one entry per address; bits are `0`/`1`. `ts` is
the device uptime (ms) of the read.

```json
{"type":"snapshot","id":"motor1","table":"holding","slave":1,"startAddress":1,"count":3,"values":[40,7,96],"ts":81234}
{"type":"update","id":"motor1","changes":[[3,53]],"ts":81434}
{"type":"error","id":"motor1","table":"holding","slave":1,"error":"slave_timeout","message":"...","modbusCode":226,"modbusError":"ResponseTimedOut","retryInMs":1000}
```

**Keeping state:** apply `snapshot` as the full state of the range and each
`update` on top of it. After an `error`, keep the last values but show them
as stale; the next `snapshot` replaces them.

### Error codes

Bus errors use the same codes as the REST API (`slave_timeout`,
`illegal_address`, `illegal_function`, `illegal_value`, `slave_failure`,
`bad_response`). Request errors: `invalid_message`, `invalid_parameter`,
`unknown_subscription`, `too_many_subscriptions`, `too_many_points`,
`too_many_blocks`, `too_many_clients`, `busy`.

## How polling works

- **One bus, one transaction at a time.** At 9600 baud a read of ~10
  registers takes ~50 ms, so the whole bus does ~20 reads/s, shared by all
  clients and the REST API.
- **Deduplication:** subscriptions on the same slave+table that overlap or
  touch are merged into one read (up to 125 registers / 2000 bits), polled
  at the fastest interval among them. Ranges with a gap in between are
  polled separately -- the slave may not define the addresses in between.
- **Bus budget:** polling may use up to 70% of the bus; the rest stays free
  for REST. If subscriptions ask for more, every interval is stretched by
  the same factor and clients are told via `effectiveIntervalMs`
  (`interval` message). `/api/health` shows the current `busLoadPct`.
- **Only changes are sent**, and never more often than each subscription's
  own `intervalMs`. A dashboard with no changes generates no traffic.
- **Slow clients:** when a client's send queue is full, its updates are
  skipped and it gets a fresh `snapshot` once it catches up.
- **Failing slaves back off:** a range that fails is retried after 250 ms,
  then 500 ms, 1 s, 2 s, up to 5 s (`retryInMs`), so a dead device doesn't
  stall the others. The response timeout is `MODBUS_RESPONSE_TIMEOUT_MS`
  (`config.h`, 400 ms).
- **REST reuse:** a REST `GET` fully covered by a range polled within its
  interval (≤ 1 s ago) is answered from that data without touching the bus;
  the response then has `"cached": true` and `ageMs`.

## Limits

| | |
|---|---|
| WebSocket connections | 4 |
| Subscriptions per connection | 8 |
| Subscriptions on the gateway | 32 |
| Distinct polled ranges (after merging) | 16 |
| Addresses subscribed in total | 4000 |

## Client example

Subscriptions live only as long as the connection: resubscribe after
reconnecting.

```js
const subs = [{ op: 'subscribe', id: 'motor1', table: 'holding', start: 1, count: 7, intervalMs: 200 }];
const state = {};

function connect() {
  const ws = new WebSocket('ws://modbus-gateway.local/ws');
  ws.onopen = () => subs.forEach((s) => ws.send(JSON.stringify(s)));
  ws.onclose = () => setTimeout(connect, 1000);
  ws.onmessage = ({ data }) => {
    const msg = JSON.parse(data);
    if (msg.type === 'snapshot') {
      state[msg.id] = {};
      msg.values.forEach((v, i) => (state[msg.id][msg.startAddress + i] = v));
    } else if (msg.type === 'update') {
      for (const [address, value] of msg.changes) state[msg.id][address] = value;
    } else if (msg.type === 'error') {
      console.warn(msg.id, msg.error, msg.retryInMs);
    }
  };
}
connect();
```
