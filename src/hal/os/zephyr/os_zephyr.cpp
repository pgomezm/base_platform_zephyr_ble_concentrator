/// @addtogroup grp_hal_os
/// @{
///
/// @file os_zephyr.cpp
///
/// Source file that implements the OS HAL on Zephyr.

#include "hal/os/os.hpp"

#include "utils/log/log.hpp"

#include <zephyr/kernel.h>

LOG_MODULE_DEFINE(hal_os);

namespace hal::os
{
namespace
{

/// How many `Thread`s this firmware creates: one active object per module
/// (app, acquisition, comms, system_diagnostics), with headroom. Bump this if
/// another active object is added.
constexpr size_t MAX_THREADS = 8U;

/// The stack pool every Thread::create() draws from, one entry per thread.
/// `K_THREAD_STACK_ARRAY_DEFINE` is the one place in this file that could not
/// be expressed as a plain aligned byte array: it also reserves Zephyr's
/// MPU stack-guard region, which a hand-rolled buffer would silently omit.
K_THREAD_STACK_ARRAY_DEFINE(s_stack_pool, MAX_THREADS, Thread::STACK_SIZE);

/// Index of the next free stack in the pool.
size_t s_next_stack_index = 0U;

/// The callback registered through register_idle_callback(), or nullptr.
IdleCallback s_idle_callback = nullptr;

/// Whether the idle-approximation thread has been started yet.
bool s_idle_thread_started = false;

/// Stack for the idle-approximation thread.
K_THREAD_STACK_DEFINE(s_idle_thread_stack, 512U);

/// Control block for the idle-approximation thread.
struct k_thread s_idle_thread_data;

/// Zephyr has no public "idle hook": its real idle thread is internal to the
/// kernel and not meant to run arbitrary application code. This thread
/// approximates one instead, at the lowest priority the build allows, so it
/// only actually runs when every other thread is blocked or sleeping. See the
/// caveat on register_idle_callback() in os.hpp.
/// Given C language linkage because Zephyr calls it through a C function
/// pointer. On this target it would work either way — C and C++ share the
/// calling convention — but the standard does not promise that, and the marker
/// is where the C boundary is documented. Do not remove it as redundant.
extern "C" void idle_approximation_thread(void*, void*, void*)
{
    while (true)
    {
        if (s_idle_callback != nullptr)
        {
            s_idle_callback();
        }

        k_yield();
    }
}

/// What a thread needs to start, kept on this side so nothing has to be cast.
struct ThreadStart
{
    ThreadEntry entry;
    void* p_arg;
};

/// One slot per thread, alongside the stack pool and indexed the same way.
ThreadStart s_thread_starts[MAX_THREADS] = {};

/// The backend's thread entry point takes three arguments; hal::os promises
/// one. This adapts between them, the same way expiry_trampoline does for
/// timers.
///
/// It is a real function with the backend's signature rather than a cast of the
/// caller's. Casting a function pointer to a different signature and calling
/// through it is undefined behaviour, even on a target whose calling convention
/// makes it work by accident.
void thread_trampoline(void* p_start, void* p_unused_1, void* p_unused_2)
{
    (void)p_unused_1;
    (void)p_unused_2;

    const auto* const p_this_start = static_cast<const ThreadStart*>(p_start);

    if ((p_this_start != nullptr) && (p_this_start->entry != nullptr))
    {
        p_this_start->entry(p_this_start->p_arg);
    }
}

/// User data stored per timer: the callback and its context, packed together
/// since Zephyr's k_timer only offers one user-data slot.
struct TimerUserData
{
    TimerCallback callback;
    void* p_context;
};

/// Runs in Zephyr's system timer ISR context. Hands off and returns.
/// Given C language linkage because Zephyr calls it through a C function
/// pointer. On this target it would work either way — C and C++ share the
/// calling convention — but the standard does not promise that, and the marker
/// is where the C boundary is documented. Do not remove it as redundant.
extern "C" void timer_expiry_handler(struct k_timer* p_timer)
{
    auto* const p_data = static_cast<TimerUserData*>(k_timer_user_data_get(p_timer));

    if ((p_data != nullptr) && (p_data->callback != nullptr))
    {
        p_data->callback(p_data->p_context);
    }
}

} // namespace

// --- Thread ------------------------------------------------------------

static_assert(sizeof(struct k_thread) <= sizeof(ThreadStorage{}.bytes),
              "hal::os::ThreadStorage is too small for struct k_thread");

Thread::Thread() : m_storage{}
{}

void Thread::create(ThreadEntry entry, void* p_arg, Priority priority, const char* p_name)
{
    auto* const p_thread_data = reinterpret_cast<struct k_thread*>(m_storage.bytes);

    if (s_next_stack_index >= MAX_THREADS)
    {
        // Every caller in this firmware is known at link time (see
        // MAX_THREADS above), so this can only mean the pool constant fell
        // out of sync with the number of active objects created.
        LOG_ERROR("hal::os thread stack pool exhausted");
        return;
    }

    const size_t index = s_next_stack_index;
    ++s_next_stack_index;

    k_thread_stack_t* const p_stack = s_stack_pool[index];

    s_thread_starts[index] = ThreadStart{entry, p_arg};

    const k_tid_t thread_id = k_thread_create(p_thread_data,
                                              p_stack,
                                              K_THREAD_STACK_SIZEOF(s_stack_pool[0]),
                                              thread_trampoline,
                                              &s_thread_starts[index],
                                              nullptr,
                                              nullptr,
                                              static_cast<int>(priority),
                                              0,
                                              K_NO_WAIT);

    k_thread_name_set(thread_id, p_name);
}

// --- Queue -------------------------------------------------------------

static_assert(sizeof(struct k_msgq) <= sizeof(QueueStorage{}.bytes),
              "hal::os::QueueStorage is too small for struct k_msgq");

Queue::Queue() : m_storage{}
{}

void Queue::init(uint8_t* p_buffer, size_t item_size, size_t max_items)
{
    auto* const p_msgq = reinterpret_cast<struct k_msgq*>(m_storage.bytes);

    k_msgq_init(p_msgq, reinterpret_cast<char*>(p_buffer), item_size, max_items);
}

bool Queue::put(const void* p_item, bool from_isr)
{
    (void)from_isr;

    // K_NO_WAIT: an active object never blocks a producer, from thread or ISR
    // context alike. On Zephyr, k_msgq_put(..., K_NO_WAIT) is already
    // ISR-safe, so there is nothing extra to do for the from_isr case.
    auto* const p_msgq = reinterpret_cast<struct k_msgq*>(m_storage.bytes);

    return k_msgq_put(p_msgq, p_item, K_NO_WAIT) == 0;
}

bool Queue::get(void* p_item)
{
    auto* const p_msgq = reinterpret_cast<struct k_msgq*>(m_storage.bytes);

    // K_FOREVER: this is the one place an active object's thread blocks, and
    // it blocks waiting for work, which is the whole point of the thread.
    return k_msgq_get(p_msgq, p_item, K_FOREVER) == 0;
}

bool Queue::try_get(void* p_item)
{
    auto* const p_msgq = reinterpret_cast<struct k_msgq*>(m_storage.bytes);

    return k_msgq_get(p_msgq, p_item, K_NO_WAIT) == 0;
}

// --- Semaphore ---------------------------------------------------------------

static_assert(sizeof(struct k_sem) <= sizeof(SemaphoreStorage{}.bytes),
              "SemaphoreStorage is too small for this Zephyr's struct k_sem");

Semaphore::Semaphore() : m_storage{}
{}

void Semaphore::init(uint32_t initial_count, uint32_t max_count)
{
    auto* const p_sem = reinterpret_cast<struct k_sem*>(m_storage.bytes);

    (void)k_sem_init(p_sem, initial_count, max_count);
}

void Semaphore::give(bool from_isr)
{
    // k_sem_give() is safe from an ISR on Zephyr, so the flag changes nothing
    // here. It is in the interface because it changes everything on FreeRTOS,
    // where the call is xSemaphoreGiveFromISR() with a different signature and
    // a yield request. A caller that has to know which one to use is a caller
    // that has to know which RTOS it is on.
    (void)from_isr;

    auto* const p_sem = reinterpret_cast<struct k_sem*>(m_storage.bytes);

    k_sem_give(p_sem);
}

bool Semaphore::take(uint32_t timeout_ms)
{
    auto* const p_sem = reinterpret_cast<struct k_sem*>(m_storage.bytes);

    const k_timeout_t timeout = (timeout_ms == Semaphore::WAIT_FOREVER) ? K_FOREVER
                                                                        : K_MSEC(timeout_ms);

    return k_sem_take(p_sem, timeout) == 0;
}

void Semaphore::reset()
{
    auto* const p_sem = reinterpret_cast<struct k_sem*>(m_storage.bytes);

    k_sem_reset(p_sem);
}

// --- Mutex -------------------------------------------------------------------

static_assert(sizeof(struct k_mutex) <= sizeof(MutexStorage{}.bytes),
              "MutexStorage is too small for this Zephyr's struct k_mutex");

Mutex::Mutex() : m_storage{}
{}

void Mutex::init()
{
    auto* const p_mutex = reinterpret_cast<struct k_mutex*>(m_storage.bytes);

    (void)k_mutex_init(p_mutex);
}

void Mutex::lock()
{
    auto* const p_mutex = reinterpret_cast<struct k_mutex*>(m_storage.bytes);

    // K_FOREVER, and no return value: a lock this firmware cannot take is a
    // lock someone is holding forever, which is a bug to find rather than an
    // error to handle. Every critical section behind this is a few field
    // copies.
    (void)k_mutex_lock(p_mutex, K_FOREVER);
}

void Mutex::unlock()
{
    auto* const p_mutex = reinterpret_cast<struct k_mutex*>(m_storage.bytes);

    (void)k_mutex_unlock(p_mutex);
}

// --- Timer ---------------------------------------------------------------

static_assert(sizeof(struct k_timer) + sizeof(TimerUserData) <= sizeof(TimerStorage{}.bytes),
              "hal::os::TimerStorage has no room after struct k_timer for the "
              "callback/context pair");

Timer::Timer() : m_storage{}
{}

void Timer::init(TimerCallback callback, void* p_context)
{
    auto* const p_timer = reinterpret_cast<struct k_timer*>(m_storage.bytes);
    auto* const p_user_data =
        reinterpret_cast<TimerUserData*>(m_storage.bytes + sizeof(struct k_timer));

    p_user_data->callback = callback;
    p_user_data->p_context = p_context;

    k_timer_init(p_timer, timer_expiry_handler, nullptr);
    k_timer_user_data_set(p_timer, p_user_data);
}

void Timer::start_once(uint32_t duration_ms)
{
    auto* const p_timer = reinterpret_cast<struct k_timer*>(m_storage.bytes);

    k_timer_start(p_timer, K_MSEC(duration_ms), K_NO_WAIT);
}

void Timer::start_periodic(uint32_t period_ms)
{
    auto* const p_timer = reinterpret_cast<struct k_timer*>(m_storage.bytes);

    k_timer_start(p_timer, K_MSEC(period_ms), K_MSEC(period_ms));
}

void Timer::stop()
{
    auto* const p_timer = reinterpret_cast<struct k_timer*>(m_storage.bytes);

    k_timer_stop(p_timer);
}

// --- Idle hook and misc ----------------------------------------------------

void register_idle_callback(IdleCallback callback)
{
    s_idle_callback = callback;

    if (!s_idle_thread_started)
    {
        // Started lazily, on first registration, rather than unconditionally
        // at boot: a firmware that never registers an idle callback should
        // not pay for a thread that only ever calls k_yield() in a loop.
        k_thread_create(&s_idle_thread_data,
                        s_idle_thread_stack,
                        K_THREAD_STACK_SIZEOF(s_idle_thread_stack),
                        idle_approximation_thread,
                        nullptr,
                        nullptr,
                        nullptr,
                        K_LOWEST_APPLICATION_THREAD_PRIO,
                        0,
                        K_NO_WAIT);
        k_thread_name_set(&s_idle_thread_data, "idle_hook");

        s_idle_thread_started = true;
    }
}

uint32_t get_uptime_ms()
{
    return static_cast<uint32_t>(k_uptime_get());
}

void delay_ms(uint32_t ms)
{
    k_sleep(K_MSEC(ms));
}

} // namespace hal::os

/// @}
