# Comms Service

@defgroup grp_svc_comms Comms Service
@ingroup grp_svc
@brief Builds, fragments and dispatches the uplink

One service owns everything that leaves the device. Dispatch scheduling, packet building,
fragmentation and the radio call all live here rather than in four separate services, because
they are one decision — what goes out, when, in how many pieces — and splitting them would mean
three modules agreeing about it.

## Contract

| | |
| --- | --- |
| **Owns** | The dispatch timer, the uplink wire format, the fragmentation logic, the sequence counter, and the heartbeat. |
| **Exposes** | `initialize()`, `get_port()`. Everything else is an event on the port. |
| **Depends on** | `svc/device_table` (read-only), `hal/link`, `svc/acquisition` (for the dropped-report counter). |

## Starting and stopping is an event, not a function call

The dispatch timer belongs to this service, so the application does not start it
by calling in. It posts `Event::START_DISPATCH` on entry to LISTENING and
`Event::STOP_DISPATCH` on entry to HARD_ERROR, and the timer is started and
stopped in this service's own thread.

Two functions, `start_dispatch_timer()` and `stop_dispatch_timer()`, used to do
this directly. Nothing broke — the underlying timer calls are safe from another
thread — but it meant the application's thread ran this service's code, which is
the arrangement the active object model exists to remove, and it would have
become a real fault the first time starting the timer also touched a variable
this service owns. The tell was in `hard_error.cpp`, which stopped acquisition
with an event and stopped this service with a function call, three lines apart.

**Stopping the timer is not enough on its own.** A timer that fired just before
the stop arrived has already queued a `DISPATCH_DUE` ahead of it, and stopping
the timer does not take that back out. So `STOP_DISPATCH` also clears
`s_dispatch_enabled`, and a `DISPATCH_DUE` arriving with the flag clear is
logged and dropped. That race existed before this change too, in a narrower
form; the flag is what closes it rather than just moving it.

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

**The zero case is a probe, not a wait.** At US915 DR0 the payload is 11 bytes, against a 12 byte
header — nothing this firmware could say fits there, since a BLE address alone is 6 of those 11.

The obvious response is to skip the cycle and wait for a better rate. That is what this service did,
and it is a **deadlock**: the rate a network assigns comes from the uplinks it receives, so a device
that transmits nothing is measured as nothing and is never raised. It would sit at DR0 for ever, and
it would do so with a perfect antenna.

So the cycle transmits an empty frame instead — no application payload, which is the one thing that
fits in 11 bytes and is exactly what LoRaWAN provides it for. Two or three of those and the network
has enough to raise the rate.

There is a second case between the two: the header fits but a record does not. That cycle sends the
header alone, flagged `HEARTBEAT`, which carries the counters and feeds the same negotiation.

**One device is not a special case.** With a 12 byte header and 25 byte records, a single endpoint
needs 37 bytes, which DR1 already provides:

| data rate | payload | records |
| --- | --- | --- |
| DR0 | ~11 B | 0 — empty probe only |
| DR1 | ~53 B | 1 |
| DR2 | ~125 B | 4 |
| DR3 | ~242 B | 9 |

DR0 is not "too small for a busy room", it is too small for anyone. Everything from DR1 up works,
which is why getting off DR0 is the whole problem.

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

The practical consequence is worth stating plainly. At DR3 a 25 byte record gives about 9 records
per fragment, and the LoRa allowance is 3 fragments per cycle, so **one cycle reports about 27
devices**. A room of 20 goes out in a single pass; a room of 50 takes two cycles, and a given
device is then reported every 30 minutes rather than every 15. Shorten the period or fix the link
budget before raising the allowance, and if you raise it, measure the airtime at the data rate the
network actually negotiated — the arithmetic that makes 3 comfortable at DR3 is not the same one
at DR0.

## Only what changed

`snapshot()` returns devices whose reading has not reached the network yet, not every fresh device.
A device that has not advertised since its last successful uplink adds nothing — the table holds a
last value, so repeating it restates what the far end already has, at 25 bytes of airtime a record.

Delivery is acknowledged, not assumed. `send_fragment()` calls `device_table::mark_reported()` for
each record **after** the transport accepted the packet, passing back the `update_seq` the uplink
actually carried. If the device advertised again while the packet was in flight its sequence has
moved on, so the entry stays pending and the fresher reading goes out next cycle. Clearing at
snapshot time instead would drop a reading every time the radio refused a packet — exactly when
losing one matters most.

## The heartbeat

Reporting only what changed means a quiet room produces no uplink at all, and from the far end that
is indistinguishable from a dead concentrator. **The rule is that the device is never silent**: once
`CONFIG_APP_HEARTBEAT_MAX_SILENCE_S` has passed with nothing sent, the next cycle transmits a header
with no records and the `HEARTBEAT` flag set. The dropped-report and eviction counters ride along,
so a heartbeat still reports whether the receive path is healthy.

**Seconds, not cycles.** An earlier version counted quiet dispatch cycles, which meant one setting
bounded silence at an hour on the LoRa build and two minutes on the TCP one, and moving the dispatch
period moved this with it. What matters is how long the far end may hear nothing, and that number
belongs to the backend: set it below whatever timeout raises an "offline" alarm there, or a quiet
installation will trip it.

`s_last_uplink_uptime_s` is the clock, reset inside `send_fragment()` on success — the one place
every application-visible uplink passes through. The empty ADR probe deliberately does not reset it:
it carries no application payload, so the far end learns nothing from it. At a data rate where only
the probe fits the device really is silent to the application, and hiding that would hide the fault
worth seeing.

Setting the symbol to 0 disables heartbeats. Only safe if something else on the network already
proves the device is alive.

## What goes on the wire

`UplinkHeader` (12 bytes) then N x `EndpointRecord` (13 bytes each), defined in
`comms_protocol.hpp`.

The header carries the dropped-report and evicted-device counters. Two bytes to make data loss
observable is worth it: without them, a room busier than the pool depth produces a short device
list that looks exactly like a quiet room.

Records carry `seconds_since_seen` rather than a timestamp, because the concentrator has no wall
clock. An absolute time whose epoch is this device's own boot would mean nothing on the far end.
