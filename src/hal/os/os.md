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

`eda/` is meant to outlive the RTOS underneath it. Without a seam, every file in it would
`#include <zephyr/kernel.h>` directly, and moving to another kernel would mean rewriting the whole
layer instead of writing one new backend here. The thread, queue and timer APIs of two RTOSes are
rarely alike in shape, so the rewrite would be real work, not a rename.

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
for.

## Semaphore and Mutex

Added because they were missing, and what is missing gets worked around. Before
they existed, `svc::device_table` guarded its table with `K_MUTEX_DEFINE`,
`svc::acquisition` ran its report pool on `k_msgq_*`, and the Wi-Fi backend
waited on `k_sem_take()`. Two of those are services, above the HAL entirely.
Nobody chose to bind them to Zephyr; there was simply nowhere else to go.

Both follow the same shape as `Thread`, `Queue` and `Timer`: an opaque
fixed-size storage struct the caller owns as a member, a `static_assert` in the
backend that the storage is big enough, and no allocation anywhere.

**`Semaphore` is for adapting a driver, not for talking between services.** Its
one use is `hal::link`'s Wi-Fi backend: the association result arrives as a
net_mgmt event on another thread, and `ILink::connect()` is specified to block
until it knows, because the LoRa backend's join blocks too. One contract for both
transports is the point of the interface. A *service* blocking on a semaphore
would be a service that has stopped answering its port — that is what the EDA is
for.

`give()` takes a `from_isr` flag that Zephyr ignores, because `k_sem_give()` is
already ISR-safe. It is in the interface because FreeRTOS does not ignore it:
there the call is `xSemaphoreGiveFromISR()`, with a different signature and a
yield request. A caller that has to know which one to use is a caller that has to
know which RTOS it is on.

**`Mutex` is for state two threads share and neither owns.** `svc::device_table`
is the only case: written by the acquisition thread, read by the comms thread,
with no behaviour of its own to justify a thread and a port. `lock()` waits
forever and returns nothing — a lock this firmware cannot take is a lock someone
is holding forever, which is a bug to find rather than an error to handle.

`init()` has to be called, which `K_MUTEX_DEFINE` did not. That is not an
oversight: FreeRTOS has no build-time equivalent, `xSemaphoreCreateMutexStatic()`
is a call, and an interface that only one backend can implement is not an
interface.

## Queue::try_get

`get()` blocks forever, which is right for an active object's thread: it blocks
waiting for work and that is the whole purpose of the thread.

`try_get()` is for a consumer already awake for another reason.
`svc::acquisition` is woken by its port when a report arrives and then drains
what accumulated; blocking on the queue as well would give the thread a second
place to wait, and an active object waits in exactly one.
