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
`hal::os::register_idle_callback()` and never touches a kernel type directly.

## Two backends exist; one is built

`hal/os/zephyr/os_zephyr.cpp` is what this firmware actually runs on, and is the only backend
`CMakeLists.txt` compiles. `hal/os/freertos/os_freertos.cpp` also exists, implementing the same
interface with `xTaskCreateStatic`, `xQueueCreateStatic` and `xTimerCreateStatic` — proof that the
seam works for more than one backend, and a ready starting point if this project ever moves back to
FreeRTOS. It is deliberately left out of the build (see the comment in `CMakeLists.txt` next to the
Zephyr sources): this project has no FreeRTOS toolchain to compile it against, so its
`static_assert`s (on `QueueStorage`/`TimerStorage` sizing) and its priority-inversion constant
(`k_priority_floor`) are unverified. Whoever wires this project up for FreeRTOS should treat that
file as a first draft to compile and fix, not as tested code — see the file's own header comment
for the specific FreeRTOSConfig.h settings and assumptions it depends on.

Adding a third backend (or any other module gaining a `zephyr/`+`freertos/` split, e.g. a
hypothetical `hal/system/freertos/`) follows the same shape: implement the interface header,
nothing above it changes.

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
