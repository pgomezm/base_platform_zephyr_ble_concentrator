# GPIO HAL

@defgroup grp_hal_gpio GPIO HAL
@brief Hardware abstraction for the board's GPIO pins

## Owns

One `IGpio` per pin the firmware drives, and the `IManager` that hands them out.
Pin configuration happens once, in the manager's constructor.

## Exposes

`hal::gpio::ManagerFactory::get_instance()`, returning an `IManager`;
`IManager::get_gpio(GpioInstances)`; and on each pin `read()`, `write()`,
`toggle()`, `set_callback()`.

## Depends on

The platform SDK only. Nothing in `hal/gpio` knows what a pin is *for*: the
names in `GpioInstances` say what is wired there, and `hal::led` is what gives
one of those pins the behaviour of an LED.

## Why this module exists

`hal::led` used to talk to Zephyr's GPIO driver directly, which meant the LED
module was platform code. Splitting the pin out leaves `hal/led/led.cpp` with no
platform dependency at all — it compiles unchanged against any backend that
provides an `IGpio` — and gives every future pin-driven peripheral the same
seam. This is the shape `deepsight-polaris-software` uses, and the reason it
uses it.

## Constraints

No `new`, no `malloc`: the manager owns every pin as a member and is itself a
function-local static.

`set_callback()` is declared because `IGpio` declares it, and stores the pointer
without doing anything else. Every GPIO here is an output; wiring interrupts up
means adding a `gpio_callback` and an interrupt configuration, which is
deliberately not done speculatively.
