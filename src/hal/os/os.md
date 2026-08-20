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

## One backend today, and what a second one would cost

`hal/os/zephyr/os_zephyr.cpp` is the only backend, and the only one
`CMakeLists.txt` compiles. A FreeRTOS backend was written and then removed: it
could not be compiled here (this project has no FreeRTOS toolchain), so its
`static_assert`s and its priority-inversion constant were unverifiable, and
sizing `QueueStorage`/`TimerStorage` to fit both backends cost 320 B of RAM for
a backend nothing ran. Unverifiable code that costs real RAM is worse than no
code.

The seam itself stays, because `eda/` needs *some* kernel wrapper either way and
this one is a single header. Adding a backend later means writing
`hal/os/<rtos>/os_<rtos>.cpp` against `os.hpp` and listing it in
`CMakeLists.txt` instead of the Zephyr one; nothing above `hal/os` changes.
That is the whole claim this module makes, and it is worth no more than one
file.

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
