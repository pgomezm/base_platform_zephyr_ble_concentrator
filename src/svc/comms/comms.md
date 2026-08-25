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
| **Owns** | The dispatch timer, the uplink wire format, the fragmentation logic, the sequence counter, and the heartbeat. |
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

The devices that do not fit are not dropped, and nothing has to remember where the cycle stopped.
A record is acknowledged only once the fragment carrying it was accepted by the transport, so
whatever did not go out is still pending in the table and the next `snapshot()` leads with it. That
is exact rather than approximate, which is why the rotation cursor an earlier version used is gone.

The practical consequence is worth stating plainly: with a 15 minute period, 30 pending devices and
DR3 (15 records per fragment), a full pass takes two cycles, so **a given device can be reported
every 30 minutes rather than every 15**. Shorten the period or fix the link budget; do not raise
the uplink allowance without measuring the airtime it costs.

## Only what changed

`snapshot()` returns devices whose reading has not reached the network yet, not every fresh device.
A device that has not advertised since its last successful uplink adds nothing — the table holds a
last value, so repeating it restates what the far end already has, at 13 bytes of airtime a record.

Delivery is acknowledged, not assumed. `send_fragment()` calls `device_table::mark_reported()` for
each record **after** the transport accepted the packet, passing back the `update_seq` the uplink
actually carried. If the device advertised again while the packet was in flight its sequence has
moved on, so the entry stays pending and the fresher reading goes out next cycle. Clearing at
snapshot time instead would drop a reading every time the radio refused a packet — exactly when
losing one matters most.

## The heartbeat

Reporting only what changed means a quiet room produces no uplink at all, and from the far end that
is indistinguishable from a dead concentrator. After `CONFIG_APP_HEARTBEAT_AFTER_CYCLES` cycles
with nothing pending (default 4, so at most an hour of silence at the default period), the service
sends a header with no records and the `HEARTBEAT` flag set. The dropped-report and eviction
counters ride along, so a heartbeat still reports whether the receive path is healthy.

Setting the symbol to 0 disables it. That is only safe if something else on the network already
proves the device is alive.

## What goes on the wire

`UplinkHeader` (12 bytes) then N x `EndpointRecord` (13 bytes each), defined in
`comms_protocol.hpp`.

The header carries the dropped-report and evicted-device counters. Two bytes to make data loss
observable is worth it: without them, a room busier than the pool depth produces a short device
list that looks exactly like a quiet room.

Records carry `seconds_since_seen` rather than a timestamp, because the concentrator has no wall
clock. An absolute time whose epoch is this device's own boot would mean nothing on the far end.
