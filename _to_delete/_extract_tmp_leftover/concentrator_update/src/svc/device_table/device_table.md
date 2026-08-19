# Device Table Service

@defgroup grp_svc_device_table Device Table Service
@ingroup grp_svc
@brief The set of endpoint devices this concentrator currently knows about

## Contract

| | |
| --- | --- |
| **Owns** | The fixed-capacity table of last-known readings, keyed by BLE address, and the eviction counter. |
| **Exposes** | `upsert()` (acquisition only), `snapshot()` (comms only), `get_device_count()`, `get_evicted_count()`. |
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
