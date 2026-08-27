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
/// This is the seam that lets the firmware change RTOS without touching
/// anything above it. Every kernel object the rest of the firmware would
/// otherwise reach for - thread, queue, timer, semaphore, mutex - is wrapped
/// here, and no module outside a platform backend includes an RTOS header.
/// Swapping RTOS means writing a new backend under `hal/os/<rtos>/` with the
/// same interface; nothing outside this module changes.
///
/// **This header names no RTOS, and neither do its comments.** A file that
/// explains itself in terms of one kernel's API has documented a port that has
/// not happened yet, and reads as a lie on the day it has. Where a
/// justification is genuinely backend-specific - why a storage buffer is the
/// size it is - it belongs in that backend's `.cpp`, next to the
/// `static_assert` that enforces it.
///
/// Objects here are intentionally not the interface-plus-free-functions shape
/// the rest of `hal/` uses (see `hal::system`, `hal::led`): a thread, a queue
/// and a timer each need caller-owned, statically allocated storage sized for
/// the backend's control block, which a stateless free-function interface
/// cannot express. Each class below is a thin wrapper around exactly one
/// kernel object, sized for the backend's control block; the backend's
/// `static_assert` in its `.cpp` file is what proves the storage is big
/// enough.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::os
{

/// Thread priority, numbered the same way regardless of backend: lower is
/// higher priority, and 0 is the highest a firmware thread may request.
///
/// A backend whose own numbering runs the other way inverts it internally. No
/// caller has to know which kind it is on.
using Priority = int;

/// Entry point of a thread.
///
/// @param p_arg the argument passed to Thread::create()
using ThreadEntry = void (*)(void* p_arg);

/// Expiry callback of a timer.
///
/// Runs in whatever context the backend's timer service uses, which may be an
/// interrupt. It must do nothing but hand off work, and never block.
///
/// @param p_context the context pointer passed to Timer::init()
using TimerCallback = void (*)(void* p_context);

/// Callback invoked repeatedly while the backend has nothing else to run.
using IdleCallback = void (*)();

/// Opaque, fixed-size storage for a backend's thread control block.
///
/// Sized for the backend's thread control block. The backend's
/// `static_assert` is what proves it is big enough.
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
    /// bytes. One size for every active object.
    static constexpr size_t STACK_SIZE = 2048U;

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
///
/// Sized for the backend's queue control block, `static_assert`-checked
/// there.
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

    /// Dequeue one item if there is one. Never blocks.
    ///
    /// For a consumer that is already awake for another reason and wants to
    /// drain what has accumulated - `svc::acquisition` is woken by its port
    /// and then empties the report pool, rather than parking a thread on the
    /// queue itself.
    ///
    /// @param p_item where the dequeued item is copied
    /// @return true if an item was dequeued, false if the queue was empty
    bool try_get(void* p_item);

private:
    QueueStorage m_storage;
};

/// Opaque, fixed-size storage for a backend's timer control block.
///
/// Sized for the backend's timer control block plus this layer's
/// callback/context pair, `static_assert`-checked there.
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

/// Opaque, fixed-size storage for a backend's semaphore control block.
///
/// Sized for the backend's semaphore control block, `static_assert`-checked
/// there.
struct SemaphoreStorage
{
    alignas(void*) uint8_t bytes[64];
};

/// A counting semaphore, statically allocated by the caller as a member.
///
/// What this is for: turning a vendor driver's asynchronous callback into a
/// call that returns an answer. `hal::link`'s Wi-Fi backend asks the driver to
/// associate and has to wait for a net_mgmt event to say whether it worked,
/// while `ILink::connect()` is specified to block until it knows - because the
/// LoRa backend's join blocks too, and one contract for both is the point of
/// the interface.
///
/// It is deliberately **not** how services talk to each other. That is the
/// EDA's job: ports, events and active objects. A service blocking on a
/// semaphore would be a service that has stopped answering its port.
class Semaphore
{
public:
    /// Passed to take() to wait indefinitely.
    static constexpr uint32_t WAIT_FOREVER = 0xFFFFFFFFU;

    Semaphore();

    // Held by address by the backend once initialized, so no copies or moves.
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    Semaphore(Semaphore&&) = delete;
    Semaphore& operator=(Semaphore&&) = delete;

    /// Initialize the semaphore.
    ///
    /// @param initial_count the count it starts with
    /// @param max_count the count it saturates at
    void init(uint32_t initial_count, uint32_t max_count);

    /// Increment the count, waking one waiter if any.
    ///
    /// @param from_isr whether this is called from interrupt context
    void give(bool from_isr);

    /// Wait for the count to be non-zero, then decrement it.
    ///
    /// @param timeout_ms how long to wait, or WAIT_FOREVER
    /// @return true if the semaphore was taken, false if the wait timed out
    bool take(uint32_t timeout_ms);

    /// Set the count back to zero, discarding anything already given.
    ///
    /// Called before starting an operation, so a give left over from a
    /// previous one is not mistaken for this one's answer.
    void reset();

private:
    SemaphoreStorage m_storage;
};

/// Opaque, fixed-size storage for a backend's mutex control block.
///
/// Sized for the backend's mutex control block, `static_assert`-checked
/// there.
struct MutexStorage
{
    alignas(void*) uint8_t bytes[64];
};

/// A mutual-exclusion lock, statically allocated by the caller as a member.
///
/// For state two threads share and neither owns - `svc::device_table` is the
/// one case: written by the acquisition thread, read by the comms thread, with
/// no behaviour of its own to justify a thread and a port.
///
/// Anything with behaviour should be an active object instead. A lock held
/// across anything long is a design mistake this class cannot prevent.
class Mutex
{
public:
    Mutex();

    // Held by address by the backend once initialized, so no copies or moves.
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    /// Initialize the mutex, unlocked.
    void init();

    /// Take the lock, blocking until it is available.
    void lock();

    /// Release the lock.
    void unlock();

private:
    MutexStorage m_storage;
};

/// Register the function invoked repeatedly while the backend has nothing
/// else to run. Only one callback may be registered; registering again
/// replaces it.
///
/// A backend with a real idle hook calls straight through to it. One without
/// approximates it with a dedicated lowest-priority thread that calls the
/// callback and yields in a loop - close, but not identical: such a thread
/// competes for the CPU like any other and does not run with interrupts masked
/// the way a true idle hook might. Which kind this build has is stated in the
/// backend's `.cpp`.
///
/// Nothing in this firmware depends on the distinction today. `eda::IdleHook`
/// is wired up because it was asked for, with no callback registered by
/// default.
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
