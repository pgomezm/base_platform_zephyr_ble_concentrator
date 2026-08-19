# LoRa HAL

@defgroup grp_hal_lora LoRa HAL
@ingroup grp_hal
@brief Hardware abstraction for the LoRa radio

Wraps Zephyr's `CONFIG_LORA` / `CONFIG_LORAWAN` subsystem, which is itself the Lora-net
LoRaMac-node stack already ported to Zephyr and pulled in through `west`.

## Contract

| | |
| --- | --- |
| **Owns** | The SX1276 radio: device init, network join, transmit, payload-size query. |
| **Exposes** | `initialize()`, `join()`, `is_joined()`, `send()`, `get_max_payload_size()`. |
| **Depends on** | Zephyr's `lorawan.h` only. |

## Constraints

**`send()` has exactly one caller: `svc::comms`.** This is the one constraint in the design that
breaks *silently* rather than loudly if violated: two contexts writing to the same SPI radio
produce corruption, not a compile error. Nothing else calls it, including
`svc::system_diagnostics`, which may query status but never transmits.

**No retry or backoff policy lives here.** This module reports what the radio did and stops.
Deciding whether to retry, and when, is a dispatch-policy question that belongs to `svc::comms`.

## Hardware

Modtronix inAir9 (Semtech SX1276, RFO output) on the nRF52840 DK's Arduino header. Pin assignment
and the reasoning behind it are in `boards/nrf52840dk_nrf52840.overlay`.

## Open

`join()` is not implemented: OTAA vs ABP, and the credentials, are undecided pending the choice of
network server. It returns `Result::JOIN_ERROR` and logs a warning rather than pretending to
succeed, so the state machine's error path is exercised instead of the firmware silently believing
it is connected.
