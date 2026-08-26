# Assert

@defgroup grp_assert Assert
@brief A halt-on-impossible-condition macro

## Owns

Nothing. One macro, no state, no code in a release build.

## Exposes

`ASSERT_CRITICAL(condition)`.

## Why it exists

Some things in this firmware cannot be allowed to fail quietly. Posting
`STOP_DISPATCH` to a full event queue is the example that brought this module
in: the event is dropped, `svc::comms` never hears it, and a concentrator that
has entered HARD_ERROR keeps transmitting. Counting the drop makes it visible
after the fact; asserting makes it stop being a thing that has to be noticed.

Ported from `deepsight-polaris-software`, where the same macro already exists.

## What it is not

**It is not error handling.** It is compiled out in a release build, so a
condition that has any chance of being false in the field needs a real branch,
not an assert. Use it where being false means the firmware is wrong, not where
it means the world is.

**The condition must have no side effects.** In a release build the whole
expression disappears, the argument included.

## What happens when one fires

Debug build: the device spins inside the failing function. Attach a debugger and
the stack is still the one that got there.

With `svc::system_diagnostics` running, the spin also stops feeding the
watchdog, so a few seconds later the device resets and `hal::system` reports a
watchdog reset. That is the intended field behaviour of a debug unit: it does
not sit there dead, and the reset reason says an assert is the likely cause.
