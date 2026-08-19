# OS HAL

@defgroup grp_hal_os OS HAL
@brief Hardware abstraction for the kernel primitives `eda/` runs on

## Owns

The thread, queue and timer control blocks `eda/` allocates: `hal::os::Thread`, `hal::os::Queue`,
`hal::os::Timer`, plus the one idle-time callback slot exposed through `register_idle_callback()`.

## Exposes

`hal::os::Thread::create()`, `hal::os::Queue::init()/put()/get()`, `hal::os::Timer::init()/
start_once()/start_periodic()/stop()`, `hal::os::register_idle_callback()`,
`hal::os::get_uptime_ms()`, `hal::os::delay_ms()`.

## Depends on

Nothing above it. This is the lowest layer in the firmware: `eda/` depends on it, and it depends
only on the platform SDK (Zephyr today).

## Constraints

No `new`, no `malloc`. Every `Thread`/`Queue`/`Timer` is a fixed-size byte buffer sized for the
backend's control block, with a `static_assert` in the backend's `.cpp` proving the buffer is
large enough; there is no dynamic allocation to get wrong.

## Why this module exists

`eda/` is ported from `deepsight-polaris-software`, which is FreeRTOS
firmware. This concentrator runs on Zephyr, whose thread/queue/timer API is unrelated in shape to
FreeRTOS's. Without a seam, every file in `eda/` would `#include <zephyr/kernel.h>` directly, and
moving back to FreeRTOS later would mean rewriting `eda/` a second time instead of writing one new
backend here.

`hal::os` is that seam. `eda/` calls `hal::os::Thread`, `hal::os::Queue`, `hal::os::Timer` and
`hal::os::register_idle_callback()` and never touches a kernel type directly. A FreeRTOS backend
would live at `hal/os/freertos/os_freertos.cpp`, implementing the same interface with
`xTaskCreateStatic`, `xQueueCreateStatic` and `xTimerCreateStatic`, the same way
`hal/system/zephyr/` and a hypothetical `hal/system/freertos/` would both implement `hal::system`.
No other module in the firmware would need to change.

## The idle hook is an approximation, not a real one

FreeRTOS has a genuine idle hook: `vApplicationIdleHook()`, called from FreeRTOS's own idle task.
Zephyr has no equivalent — its idle thread is internal to the kernel and is not a place application
code is meant to run. `register_idle_callback()` approximates one with a dedicated thread at the
lowest priority the build allows (`os_zephyr.cpp`, `idle_approximation_thread()`), which calls the
registered callback and yields, in a loop. It only actually runs once every other thread is
blocked or sleeping, which is the property that matters for `eda::IdleHook`'s contract, but it is
a real thread competing for the scheduler, not a hook running with the CPU otherwise doing nothing.
Nothing in this firmware registers a callback today; the mechanism exists because it was asked
for, matching `deepsight-polaris-software`'s `eda::IdleHook`.
