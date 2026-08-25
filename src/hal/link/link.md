# Link HAL

@defgroup grp_hal_link Link HAL
@brief Hardware abstraction for the uplink transport

## Owns

The one transport instance the firmware sends through, and the downlink callback
slot.

## Exposes

`hal::link::LinkFactory::get_instance()`, returning an `ILink`; on it
`initialize()`, `connect()`, `is_connected()`, `send()`,
`register_downlink_callback()`, `get_max_payload_size()`,
`get_max_uplinks_per_dispatch()`.

## Depends on

The platform SDK only.

## Why this module exists

The concentrator ships in more than one variant, and the only thing that differs
between them is how collected readings leave the device: LoRaWAN on the
nRF52840 build, TCP on a board with a wired network. Acquisition, the device
table, the state machine and the uplink wire format are identical.

Naming the module after the transport — the `hal::lora` it started as — would
have forced the second variant to be a fork of the whole repository, and every
fix to `eda/` or `svc::acquisition` would then have to be applied twice.
`hal::link` is the seam that keeps it one repository with two builds.

## Constraints

**`svc::comms` is the only caller of `send()`.** One writer keeps send order
unambiguous, and on the LoRa backend it keeps two contexts off the same SPI bus.
See docs/ARCHITECTURE.md section 4.

Nothing above this module may name a transport. An event called
`NETWORK_JOIN_FAILED` would be leaking LoRaWAN vocabulary upward; the events are
`NETWORK_JOINED`/`NETWORK_JOIN_FAILED` today for continuity, and their doc
comments are written in transport-neutral terms.

`get_max_payload_size()` returns a `uint8_t`, which is the LoRaWAN ceiling
(242 B). A transport with no meaningful limit should report the largest fragment
it wants to see rather than the largest it could carry, so `svc::comms`
fragments the same way on both.

## How much a transport is willing to send at once

`get_max_payload_size()` answers "how big may one packet be". It does not answer
"how many packets may I send right now", and those are different questions on a
radio.

A dispatch that needs four fragments used to send four packets back to back,
because nothing said it could not. Over TCP that is free. Over LoRaWAN it is
four separate transmissions from one node with no gap between them: at DR0 a
single 11-byte packet already holds the channel for roughly a quarter of a
second, so a full table is seconds of continuous airtime. That is what gets a
device throttled by the network server, and refused outright wherever a duty
cycle is enforced.

`get_max_uplinks_per_dispatch()` is the second question, asked separately. The
LoRa backend answers 1 by default; the TCP backend answers `UINT8_MAX`. The
limit lives here rather than in `svc::comms` because it is a property of the
transport, not of the data.

What makes it safe to defer the rest is the shape of the device table: it holds
the **last value per device**, so a record not sent this cycle goes out in the
next one carrying a fresher reading than the one it replaced. The cost of a low
limit is latency, not data. None of this would hold if the table kept history.

## Backends

| backend | source | selected by | status |
| --- | --- | --- | --- |
| LoRaWAN | `lora/link_lora.cpp` | `CONFIG_APP_LINK_LORA` (default) | OTAA join verified against a MultiTech Conduit; uplink still blocked by the antenna, see `docs/BRINGUP.md` |
| TCP | `tcp/link_tcp.cpp` | `CONFIG_APP_LINK_TCP` | compiles; never run against real hardware |

`CMakeLists.txt` compiles exactly one, and the Kconfig choice also selects the
Zephyr subsystem that backend needs — `LORA`/`LORAWAN` for one, `NETWORKING`/
`NET_TCP`/`NET_SOCKETS` for the other. That is why `prj.conf` mentions neither:
the transport is one symbol, not a block of them.

Build the TCP variant with:

```sh
west build -b nrf52840dk/nrf52840 <app> -- -DCONFIG_APP_LINK_TCP=y
```

It builds without any Ethernet hardware present. It will not *run* without a
network interface — `initialize()` reports `NOT_READY` when
`net_if_get_default()` returns nothing — but keeping it compiling from the start
is what stops the TCP path from rotting while the hardware is being decided.

## Two things the TCP backend does not do yet

**No downlink.** `register_downlink_callback()` stores the callback and nothing
delivers to it. Receiving would need a reader thread parked in `zsock_recv()`,
and what a downlink means over a stream with no framing is undecided. It is
accepted rather than rejected so `svc::comms` is written identically on both
backends.

**No reconnect strategy of its own.** A failed `send()` closes the socket and
reports `SEND_ERROR`; rebuilding the connection is the next `connect()`, which
the state machine's error path already drives. The transport does not retry
behind anyone's back.

## Why the TCP link reports a payload limit it does not have

`get_max_payload_size()` returns `CONFIG_APP_LINK_TCP_MAX_FRAGMENT`, default 242
— the LoRaWAN ceiling. TCP has no such limit, but declaring one keeps
`svc::comms` fragmenting exactly as it does on LoRaWAN, so both variants emit the
same wire format and there is one fragmentation path to test instead of two. A
fragment built by the TCP variant is always one a LoRa build could also have
sent.
