# System Diagnostics Service

@defgroup grp_svc_system_diagnostics System Diagnostics Service
@ingroup grp_svc
@brief Heartbeat, fault annunciation and health reporting

Runs the checks that say whether the device itself is healthy, as opposed to what it is
measuring.

## Contract

| | |
| --- | --- |
| **Owns** | The heartbeat timer, the fault annunciator, and the health summary. |
| **Exposes** | `initialize()`, `get_port()`. |
| **Depends on** | `hal/led`, and read-only access to `svc/device_table` and `svc/acquisition` counters. |

## Constraints

**This service does not touch the watchdog.** It used to arm it and feed it from the
`HEARTBEAT_DUE` handler, which made the watchdog prove that the heartbeat timer was still firing —
the one thing nobody doubted. `app` arms it now and `eda::IdleHook` feeds it from idle, so what it
measures is whether the system reaches idle at all. A module that feeds a watchdog from its own
thread is vouching for itself.

**This service never transmits.** It reads link status; it does not call `hal::link::send()`. That
belongs to `svc::comms` alone.

## What it reports

Every 60 heartbeats it logs the three numbers that say whether the concentrator is keeping up: how
many devices it is tracking, how many advertising reports it dropped, and how many devices it
evicted. The last two also go out in every uplink header, so the same information is visible to
whoever is receiving the data and not just to somebody with a serial console attached.
