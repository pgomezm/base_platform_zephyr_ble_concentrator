/// @defgroup grp_hal_os OS HAL
///
/// Hardware Abstraction Layer for the kernel primitives `eda/` is built on:
/// threads, message queues, timers, and idle-time callbacks.
///
/// @addtogroup grp_hal_os
/// @{
///
/// @file os.hpp
///
/// Header file that declares the OS HAL interface.
///
/// This is the seam requested so the firmware can move back to FreeRTOS in the
/// future without touching `eda/`: every kernel type `eda/` would otherwise
/// reach for directly (`k_thread`, `k_msgq`, `k_timer`, ...) is wrapped here
/// instead. `eda/` includes this header and never includes
/// `<zephyr/kernel.h>` itself. Swapping RTOS means writing a new backend
/// under `hal/os/<rtos>/` with the same interface; nothing outside this
/// module changes.
///
/// Objects here are intentionally not the interface-plus-free-functions shape
/// the rest of `hal/` uses (see `hal::system`, `hal::led`): a thread, a queue
/// and a timer each need caller-owned, statically allocated storage sized for
/// the backend's control block, which a stateless free-function interface
/// cannot express. Each class below is a thin wrapper around exactly one
/// kernel object, sized generously enough to hold either a Zephyr or a
/// FreeRTOS control block; the backend's `static_assert` in its `.cpp` file
/// is what proves the storage is big enough.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::os
{

/// Thread priority, numbered the same way regardless of backend: lower is
/// higher priority, and 0 is the highest priority a firmware thread may
/// request. The Zephyr backend maps this straight onto Zephyr's own
/// convention; a FreeRTOS backend would invert it internally instead of
/// asking every caller to know that FreeRTOS numbers priority the other way.
using Priority = int;

/// Entry point of a thread.
///
/// @param p_arg the argument passed to Thread::create()
using ThreadEntry = void (*)(void* p_arg);

/// Expiry callback of a timer.
///
/// Runs in whatever context the backend's timer service uses: an ISR on
/// Zephyr, the timer service task on FreeRTOS. It must do nothing but hand
/// off work, never block.
///
/// @param p_context the context pointer passed to Timer::init()
using TimerCallback = void (*)(void* p_context);

/// Callback invoked repeatedly while the backend has nothing else to run.
using IdleCallback = void (*)();

/// Opaque, fixed-size storage for a backend's thread control block.
///
/// Sized to fit Zephyr's `struct k_thread`; a FreeRTOS `StaticTask_t` is
/// smaller and fits the same budget.
struct ThreadStorage
{
    alignas(void*) uint8_t bytes[192];
};

/// One thread, including its stack. Statically allocated by the caller as a
/// member, matching the static-allocation rule the rest of the firmware
/// follows: no `new`, no backend-specific stack macro at the call site.
class Thread
{
public:
    /// Stack size used for every thread created through this class, in
    /// bytes. One size for every active object, same as
    /// `deepsight-polaris-software`'s `ActiveObject::s_stack_size`.
    static constexpr size_t k_stack_size = 2048U;

    Thread();

    // Held by address by the backend once created, so no copies or moves.
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread&&) = delete;

    /// Create and start the thread.
    ///
    /// @param entry the thread's entry point
    /// @param p_arg argument passed to @p entry
    /// @param priority the thread's priority
    /// @param p_name string to identify the thread for debugging purposes
    void create(ThreadEntry entry, void* p_arg, Priority priority, const char* p_name);

private:
    ThreadStorage m_storage;
};

/// Opaque, fixed-size storage for a backend's queue control block.
struct QueueStorage
{
    alignas(void*) uint8_t bytes[64];
};

/// A fixed-capacity FIFO of fixed-size items, statically allocated by the
/// caller as a member.
class Queue
{
public:
    Queue();

    // Held by address by the backend once initialized, so no copies or moves.
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    Queue(Queue&&) = delete;
    Queue& operator=(Queue&&) = delete;

    /// Initialize the queue over caller-owned, statically allocated storage.
    ///
    /// @param p_buffer storage for `max_items` items of `item_size` bytes each
    /// @param item_size size of one item, in bytes
    /// @param max_items capacity of the queue, in items
    void init(uint8_t* p_buffer, size_t item_size, size_t max_items);

    /// Enqueue a copy of `*p_item`. Never blocks: an active object never
    /// blocks a producer.
    ///
    /// @param p_item the item to copy into the queue
    /// @param from_isr whether this is called from interrupt context
    /// @return true if the item was queued, false if the queue was full
    bool put(const void* p_item, bool from_isr);

    /// Dequeue one item, blocking until one is available.
    ///
    /// @param p_item where the dequeued item is copied
    /// @return true once an item has been dequeued
    bool get(void* p_item);

private:
    QueueStorage m_storage;
};

/// Opaque, fixed-size storage for a backend's timer control block.
struct TimerStorage
{
    alignas(void*) uint8_t bytes[64];
};

/// A timer that invokes a callback on expiry.
class Timer
{
public:
    Timer();

    // Held by address by the backend once initialized, so no copies or moves.
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    /// Initialize the timer. Does not start it.
    ///
    /// @param callback called on expiry
    /// @param p_context passed back to @p callback on expiry
    void init(TimerCallback callback, void* p_context);

    /// Start (or restart) the timer as one-shot.
    ///
    /// @param duration_ms time until expiry, in milliseconds
    void start_once(uint32_t duration_ms);

    /// Start (or restart) the timer as periodic.
    ///
    /// @param period_ms period between expiries, in milliseconds
    void start_periodic(uint32_t period_ms);

    /// Stop the timer. Safe to call when it is not running, and safe to call
    /// from interrupt context.
    void stop();

private:
    TimerStorage m_storage;
};

/// Register the function invoked repeatedly while the backend has nothing
/// else to run. Only one callback may be registered; registering again
/// replaces it.
///
/// On FreeRTOS this would call straight through to `vApplicationIdleHook()`.
/// Zephyr has no public equivalent hook, so the Zephyr backend approximates
/// one with a dedicated, lowest-priority thread (see `os_zephyr.cpp`) that
/// calls the callback and yields, in a loop. It is an approximation, not the
/// real idle thread: it does not run with interrupts masked the way a true
/// idle hook might, and it competes for the CPU like any other thread, just
/// at the lowest priority. Nothing in this firmware depends on the
/// distinction today; `eda::IdleHook` is wired up because it was asked for,
/// with no callback registered by default.
///
/// @param callback the function to call, or nullptr to unregister
void register_idle_callback(IdleCallback callback);

/// Milliseconds since boot.
uint32_t get_uptime_ms();

/// Block the calling thread for the given duration.
///
/// @param ms how long to block, in milliseconds
void delay_ms(uint32_t ms);

} // namespace hal::os

/// @}
