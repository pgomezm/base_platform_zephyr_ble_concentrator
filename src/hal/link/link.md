# Link HAL

@defgroup grp_hal_link Link HAL
@brief Hardware abstraction for the uplink transport

## Owns

The one transport instance the firmware sends through, and the downlink callback
slot.

## Exposes

`hal::link::LinkFactory::get_instance()`, returning an `ILink`; on it
`initialize()`, `connect()`, `is_connected()`, `send()`,
`register_downlink_callback()`, `get_max_payload_size()`.

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

## Backends

| backend | source | status |
| --- | --- | --- |
| LoRaWAN | `lora/link_lora.cpp` | built; `connect()` still returns `CONNECT_ERROR`, see open item 1 |
| TCP | not written yet | waiting on the board decision for the v2 hardware |

Exactly one is listed in `CMakeLists.txt`. The TCP backend is deliberately not
written ahead of knowing which board runs it: an unbuildable backend is exactly
what the FreeRTOS `hal::os` backend was before it was removed.
