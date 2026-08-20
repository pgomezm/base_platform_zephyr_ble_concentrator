# LED HAL

@defgroup grp_hal_led LED HAL
@brief Hardware abstraction for the status LEDs

## Owns

One `Led` per entry in `LedInstances`, and the `Manager` that hands them out.
Each `Led` holds a reference to the `hal::gpio::IGpio` it drives, plus its own
cached on/off state.

## Exposes

`hal::led::Manager::get_instance()`, `Manager::get_led(LedInstances)`, and on
each LED `turn_on()`, `turn_off()`, `toggle()`, `get_state()`.

## Depends on

`hal::gpio`, and nothing else. There is no `zephyr/` subdirectory under this
module and there does not need to be: every platform-specific line lives behind
`IGpio`, so `led.cpp` is portable as written.

## Constraints

No `new`, no `malloc`. `Manager` is a function-local static, constructed the
first time it is asked for, which is also when its pins get configured — there
is no separate `initialize()` anyone can forget to call. `app::App::initialize()`
touches the manager explicitly so that LED bring-up happens at a known point in
the sequence rather than the first time something happens to blink.

## Who uses which LED

| instance | driven by |
| --- | --- |
| `HEARTBEAT_LED` | `svc::system_diagnostics`, toggled once a second |
| `ACTIVITY_LED` | unused today; wired so acquisition can show traffic |
| `ERROR_LED` | `SOFT_ERROR` and `HARD_ERROR` states |
