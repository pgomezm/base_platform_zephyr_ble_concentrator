# Comms Service

@defgroup grp_svc_comms Comms Service
@ingroup grp_svc
@brief Builds, fragments and dispatches the uplink

Both `deepsight-polaris-software` and `base_platform_baremetal_ble` have exactly one `svc/comms`
that owns outbound communication, and so does this firmware. Dispatch scheduling, packet building,
fragmentation and the radio call all live here rather than in four separate services.

## Contract

| | |
| --- | --- |
| **Owns** | The dispatch timer, the uplink wire format, the fragmentation logic, and the sequence counter. |
| **Exposes** | `initialize()`, `get_port()`, `start_dispatch_timer()`, `stop_dispatch_timer()`. |
| **Depends on** | `svc/device_table` (read-only), `hal/lora`, `svc/acquisition` (for the dropped-report counter). |

## The single writer

**This service is the only caller of `hal::lora::send()`.** Two contexts writing to the same SPI
radio corrupt each other silently rather than failing loudly, so this is the constraint in the
design most worth restating: nothing else transmits, including `svc::system_diagnostics`, which may
read radio status but never sends.

## Fragmentation

At dispatch time the service asks the radio what the current data rate allows, and divides:

```
records_per_fragment = (max_payload - sizeof(UplinkHeader)) / sizeof(EndpointRecord)
```

Every fragment of one cycle carries the same sequence number and its own index, so the far end can
tell a multi-fragment cycle from several single-fragment ones.

**The zero case is a wait, not a retry.** At the lowest US915 data rate the payload can be smaller
than a header plus one record. Fragmenting into pieces the radio will refuse would loop forever, so
the service logs and skips the cycle; the next one may negotiate a better rate. The payload table in
`docs/ARCHITECTURE.md` section 5 is still flagged for verification against the regional parameters
spec, which is exactly why this path is handled rather than assumed away.

## What goes on the wire

`UplinkHeader` (12 bytes) then N x `EndpointRecord` (13 bytes each), defined in
`comms_protocol.hpp`.

The header carries the dropped-report and evicted-device counters. Two bytes to make data loss
observable is worth it: without them, a room busier than the pool depth produces a short device
list that looks exactly like a quiet room.

Records carry `seconds_since_seen` rather than a timestamp, because the concentrator has no wall
clock. An absolute time whose epoch is this device's own boot would mean nothing on the far end.
