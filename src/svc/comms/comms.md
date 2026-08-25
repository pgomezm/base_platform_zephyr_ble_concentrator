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
| **Owns** | The dispatch timer, the uplink wire format, the fragmentation logic, the sequence counter, and the cursor that says where the next cycle resumes. |
| **Exposes** | `initialize()`, `get_port()`, `start_dispatch_timer()`, `stop_dispatch_timer()`. |
| **Depends on** | `svc/device_table` (read-only), `hal/link`, `svc/acquisition` (for the dropped-report counter). |

## The single writer

**This service is the only caller of `hal::link::send()`.** Two contexts writing to the same SPI
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

## How many fragments actually go out

Knowing how many fragments a cycle *needs* is not the same as being allowed to send them. The
service asks the transport both questions:

```
fragments_needed  = ceil(total_records / records_per_fragment)
fragments_allowed = hal::link::get_max_uplinks_per_dispatch()
```

and sends the smaller of the two. On LoRaWAN `fragments_allowed` is 1, so **one dispatch cycle is
one transmission.** Sending a queue of packets back to back is airtime a single node is not
entitled to; see `src/hal/link/link.md`.

The devices that do not fit are not dropped. `s_rotation_start` remembers the first record the
cycle did not send, and the next cycle leads with it, so a full pass over the table takes as many
cycles as it takes and no device starves. Because the table holds the **last value per device**, a
record that waits a cycle goes out carrying a fresher reading than the one it would have carried —
the cost is latency, not data.

The cursor indexes the snapshot rather than the table, so it is fair only while the snapshot keeps
its ordering. It does not when a device goes stale or a new one appears, and a device can then be
skipped or repeated once. That is acceptable for latest-value data and would not be for history.

The practical consequence is worth stating plainly: with a 15 minute period, 30 devices and DR3
(11 records per fragment), a full pass takes three cycles, so **a given device is reported roughly
every 45 minutes**, not every 15. Shorten the period or raise the data rate; do not raise the
uplink allowance without measuring the airtime it costs.

## What goes on the wire

`UplinkHeader` (12 bytes) then N x `EndpointRecord` (13 bytes each), defined in
`comms_protocol.hpp`.

The header carries the dropped-report and evicted-device counters. Two bytes to make data loss
observable is worth it: without them, a room busier than the pool depth produces a short device
list that looks exactly like a quiet room.

Records carry `seconds_since_seen` rather than a timestamp, because the concentrator has no wall
clock. An absolute time whose epoch is this device's own boot would mean nothing on the far end.
