# System Diagnostics Service

@defgroup grp_svc_system_diagnostics System Diagnostics Service
@ingroup grp_svc
@brief Heartbeat, watchdog and health reporting

Same role this module has in `base_platform_baremetal_ble`: run the checks that say whether the
device itself is healthy, as opposed to what it is measuring.

## Contract

| | |
| --- | --- |
| **Owns** | The heartbeat timer, the watchdog feed, and the health summary. |
| **Exposes** | `initialize()`, `get_port()`. |
| **Depends on** | `hal/watchdog`, `hal/led`, and read-only access to `svc/device_table` and `svc/acquisition` counters. |

## Constraints

**The watchdog is fed here and nowhere else.** A module that feeds it from its own thread turns the
watchdog into proof that one thread is alive, which is not what it is for. Feeding it from the
lowest-priority thread means a reset happens when the system as a whole stops making progress.

**This service never transmits.** It reads LoRa status; it does not call `hal::lora::send()`. That
belongs to `svc::comms` alone.

## What it reports

Every 60 heartbeats it logs the three numbers that say whether the concentrator is keeping up: how
many devices it is tracking, how many advertising reports it dropped, and how many devices it
evicted. The last two also go out in every uplink header, so the same information is visible to
whoever is receiving the data and not just to somebody with a serial console attached.
