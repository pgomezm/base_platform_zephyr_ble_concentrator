# Device Table Service

@defgroup grp_svc_device_table Device Table Service
@ingroup grp_svc
@brief The set of endpoint devices this concentrator currently knows about

## Contract

| | |
| --- | --- |
| **Owns** | The fixed-capacity table of last-known readings, keyed by BLE address, the per-entry update/reported sequences, and the eviction counter. |
| **Exposes** | `upsert()` (acquisition only), `snapshot()` and `mark_reported()` (comms only), `get_device_count()`, `get_evicted_count()`. |
| **Depends on** | `hal/system` for the uptime clock. No BLE or LoRa headers. |

## Why an address is the key

The endpoint's advertisement carries no device identifier. The company id in the frame is shared
across the whole product line, so it identifies the product, not the unit. The BLE advertiser
address is the only thing that distinguishes one sensor from another, so it is the key.

## Not an active object

This is a mutex-guarded static structure, not a thread. It has state to protect and no behaviour of
its own to run, and giving it a thread would add a queue hop to every reading for nothing.

`snapshot()` copies entries out under the lock and returns. The comms thread then builds and
fragments an uplink from its own copy, holding no lock while it does, so a long dispatch never
blocks the acquisition thread from recording new readings.

## Overflow

Capacity is `CONFIG_APP_MAX_DEVICES`. A new device arriving at a full table evicts the least
recently seen entry, on the reasoning that it is the one least likely to still be in the room. Every
eviction is counted, and the count goes out in the uplink header, so a room with more devices than
the table can hold is visible as exactly that rather than as a short device list.

Entries older than `CONFIG_APP_DEVICE_STALE_AFTER_S` are skipped by `snapshot()`. They stay in the
table as eviction candidates, but they are not reported: an uplink listing a device should mean it
was heard from recently.

## Pending, and why it is a sequence rather than a flag

The table holds a **last value per device**, so a device that has not advertised since its last
successful uplink has nothing to add. `snapshot()` skips it. Each entry carries two counters:
`update_seq`, bumped by every `upsert()`, and `reported_seq`, set by `mark_reported()` once an
uplink carrying that reading was accepted by the transport. An entry is pending exactly when the
two differ. Neither is transmitted.

A plain `dirty` boolean would be one byte cheaper and wrong in one specific case. `upsert()` runs on
the acquisition thread and `mark_reported()` on the comms thread, so an advertisement can arrive
while the uplink is still in flight. A boolean cleared on acknowledgement would clear *that* newer
reading too, and it would stay unsent until the device happened to advertise again. Comparing the
sequence the uplink actually carried against the one the entry holds now makes the case visible:
they differ, so the entry stays pending. The counters wrap at 65535 and only equality is ever
tested, so wrapping is harmless.

Acknowledging after the send rather than at snapshot time is the other half of it. Marking entries
at snapshot time would lose a reading every time the transport refused the packet.

The cost of reporting only what changed is that a quiet room produces no uplink at all. `svc::comms`
covers that with a heartbeat; see `src/svc/comms/comms.md`.
