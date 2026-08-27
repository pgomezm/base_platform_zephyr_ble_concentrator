# Fault

@defgroup grp_utils_fault Fault
@ingroup grp_utils
@brief The latch that says this device has stopped being trustworthy

## Contract

| | |
| --- | --- |
| **Owns** | One flag and one reason code. |
| **Exposes** | `report()`, `is_active()`, `get_reason()`, `describe()`. |
| **Depends on** | Nothing. |

## Why it does so little

`report()` sets a flag and returns. It does not log, does not touch a LED, does
not post an event.

That is the point. The case it exists for is a one-shot event that could not be
queued, and there is no way to report *that* by sending an event: the mechanism
that failed is the one you would report through. So the latch has to be a plain
function call that works from any thread, from an interrupt, and with the event
system already broken.

Everything else — logging it, blinking it, keeping it visible — happens later,
in `svc::system_diagnostics`, on a tick it already had.

## The first reason wins

A fault tends to knock over whatever runs next, so later reports are usually
consequences. `report()` keeps the first one.

## Getting out

You do not. Nothing clears the latch; only a reset does. A device that could
talk itself back into being trusted would be a device whose fault state means
nothing.

## What it looks like

ERROR and ACTIVITY blink together, heartbeat off. The watchdog keeps being fed,
so it blinks until somebody looks at it — a device that resets itself erases
what went wrong.

On a board with no LEDs, such as the ESP32-S3 DevKitC, there is nothing to see.
The fault is in the log and nowhere else. Declared, not solved: see
docs/CODING_STANDARD.md.
