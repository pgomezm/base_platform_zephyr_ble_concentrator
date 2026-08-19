/// @addtogroup grp_hal_os
/// @{
///
/// @file os_freertos.cpp
///
/// Source file that implements the OS HAL on FreeRTOS.
///
/// NOT PART OF THE CURRENT BUILD. This firmware targets Zephyr today (see
/// `os_zephyr.cpp`); this file exists so the abstraction in `hal/os/os.hpp`
/// is proven against a second backend rather than being a one-backend-only
/// promise, and so a future move back to FreeRTOS starts here instead of
/// from nothing. `CMakeLists.txt` deliberately does not list it — see the
/// comment next to `src/hal/os/zephyr/os_zephyr.cpp` there.
///
/// This file has never been compiled: there is no FreeRTOS toolchain in this
/// project's build. Two things in particular are unverified until it is:
///   - The `static_assert`s below, proving `hal::os::QueueStorage`/
///     `TimerStorage` are large enough for `StaticQueue_t`/`StaticTimer_t`
///     plus what this backend stores alongside them. `os.hpp` sizes those
///     buffers generously for exactly this reason, but "generously" was a
///     guess informed by typical FreeRTOS struct layouts, not a measurement
///     the way the Zephyr backend's sizes are (see the note in `os.hpp`).
///   - `k_priority_floor` below, the constant the priority inversion is
///     computed from. It must exceed the numerically largest
///     `app::TaskPriorities` value in use (today: 7), and the project's
///     `FreeRTOSConfig.h` must set `configMAX_PRIORITIES` above the highest
///     priority this produces, or task creation silently clamps.
///
/// Required FreeRTOSConfig.h settings this backend assumes:
///   - `configSUPPORT_STATIC_ALLOCATION 1` (every Thread/Queue/Timer here is
///     statically allocated, matching this firmware's no-heap rule)
///   - `configUSE_IDLE_HOOK 1` (register_idle_callback() relies on the kernel
///     actually calling vApplicationIdleHook())
///   - `configUSE_TIMERS 1`, with `configTIMER_TASK_PRIORITY` and
///     `configTIMER_QUEUE_LENGTH` sized for this firmware's timers
///     (svc::comms's dispatch timer, svc::system_diagnostics's heartbeat)

#include "hal/os/os.hpp"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include "timers.h"

namespace hal::os
{
namespace
{

/// How many `Thread`s this firmware creates: one active object per module
/// (app, acquisition, comms, system_diagnostics), with headroom. Bump this if
/// another active object is added. Mirrors `os_zephyr.cpp`'s pool of the same
/// name and purpose.
constexpr size_t k_max_threads = 8U;

/// Backing storage for every thread's stack, in FreeRTOS's stack-word units.
/// FreeRTOS wants `StackType_t*`, not a byte pointer, so this is sized in
/// words rather than bytes the way `Thread::k_stack_size` is documented.
StackType_t s_stack_pool[k_max_threads][Thread::k_stack_size / sizeof(StackType_t)];

/// Control block for each pooled stack's task, one per Thread::create() call.
StaticTask_t s_task_control_blocks[k_max_threads];

/// Index of the next free entry in the pools above.
size_t s_next_thread_index = 0U;

/// Lowest (numerically largest) priority value this backend will ever be
/// asked to create a thread at, in this abstraction's own numbering (lower
/// number = higher priority, matching Zephyr's convention — see `Priority`'s
/// doc comment in os.hpp). Must stay above every `app::TaskPriorities` value
/// in use; see the file-level comment above for why this can't be verified
/// here.
constexpr Priority k_priority_floor = 10;

/// Convert this abstraction's Priority (lower = higher, Zephyr-style) to
/// FreeRTOS's (higher = higher). Keeps every caller in eda/ and above
/// ignorant of which convention the active backend actually uses.
UBaseType_t to_freertos_priority(Priority priority)
{
    return static_cast<UBaseType_t>(k_priority_floor - priority);
}

/// The callback registered through register_idle_callback(), or nullptr.
IdleCallback s_idle_callback = nullptr;

/// User data stored alongside a FreeRTOS timer: the callback and context this
/// layer promises to hand back on expiry, plus the handle FreeRTOS returned
/// from xTimerCreateStatic() (needed by start_once()/start_periodic()/stop(),
/// which only have the `Timer*`, not that handle, to work from).
struct TimerUserData
{
    TimerCallback callback;
    void* p_context;
    TimerHandle_t handle;
};

/// Runs in the FreeRTOS timer service task. Hands off and returns.
void timer_expiry_handler(TimerHandle_t timer_handle)
{
    auto* const p_data = static_cast<TimerUserData*>(pvTimerGetTimerID(timer_handle));

    if ((p_data != nullptr) && (p_data->callback != nullptr))
    {
        p_data->callback(p_data->p_context);
    }
}

} // namespace

// --- Thread ------------------------------------------------------------

// StaticTask_t is well under Zephyr's struct k_thread on every FreeRTOS port
// this project is likely to target, so ThreadStorage (sized off k_thread) is
// not the constraint here — nothing to store in it, since this backend keeps
// task control blocks in the s_task_control_blocks pool above rather than in
// each Thread's own storage. Thread::m_storage goes unused by this backend.

Thread::Thread() : m_storage{}
{
}

void Thread::create(ThreadEntry entry, void* p_arg, Priority priority, const char* p_name)
{
    if (s_next_thread_index >= k_max_threads)
    {
        // Every caller in this firmware is known at link time (see
        // k_max_threads above), so this can only mean the pool constant fell
        // out of sync with the number of active objects created. No LOG_*
        // here: this backend does not depend on the logging module the
        // Zephyr backend uses, to keep it usable in a project that has not
        // pulled that module in.
        return;
    }

    const size_t index = s_next_thread_index;
    ++s_next_thread_index;

    // TaskFunction_t and hal::os::ThreadEntry have the same signature
    // (void(*)(void*)), so this is a reinterpretation, not an adaptation.
    (void)xTaskCreateStatic(reinterpret_cast<TaskFunction_t>(entry), p_name,
                            Thread::k_stack_size / sizeof(StackType_t), p_arg,
                            to_freertos_priority(priority), s_stack_pool[index],
                            &s_task_control_blocks[index]);
}

// --- Queue -------------------------------------------------------------

/// Layout inside Queue::m_storage: the QueueHandle_t xQueueCreateStatic()
/// returns, immediately followed by the StaticQueue_t control block it
/// writes into. Kept together, like Timer's TimerUserData/StaticTimer_t
/// pair, so a Queue is fully self-contained in its own storage with no
/// separate free-standing pool the way Thread's stacks need one.
struct QueueLayout
{
    QueueHandle_t handle;
    StaticQueue_t control_block;
};

static_assert(sizeof(QueueLayout) <= sizeof(QueueStorage{}.bytes),
              "hal::os::QueueStorage is too small for FreeRTOS's StaticQueue_t "
              "plus the handle this backend stores alongside it — bump "
              "QueueStorage::bytes in os.hpp");

Queue::Queue() : m_storage{}
{
}

void Queue::init(uint8_t* p_buffer, size_t item_size, size_t max_items)
{
    auto* const p_layout = reinterpret_cast<QueueLayout*>(m_storage.bytes);

    p_layout->handle = xQueueCreateStatic(static_cast<UBaseType_t>(max_items),
                                          static_cast<UBaseType_t>(item_size), p_buffer,
                                          &p_layout->control_block);
}

bool Queue::put(const void* p_item, bool from_isr)
{
    auto* const p_layout = reinterpret_cast<QueueLayout*>(m_storage.bytes);

    if (from_isr)
    {
        BaseType_t higher_priority_task_woken = pdFALSE;

        const BaseType_t status =
            xQueueSendFromISR(p_layout->handle, p_item, &higher_priority_task_woken);

        portYIELD_FROM_ISR(higher_priority_task_woken);

        return status == pdPASS;
    }

    // 0 ticks to wait: an active object never blocks a producer, matching
    // K_NO_WAIT on the Zephyr backend.
    return xQueueSend(p_layout->handle, p_item, 0U) == pdPASS;
}

bool Queue::get(void* p_item)
{
    auto* const p_layout = reinterpret_cast<QueueLayout*>(m_storage.bytes);

    // portMAX_DELAY: this is the one place an active object's thread blocks,
    // and it blocks waiting for work, which is the whole point of the
    // thread. Matches K_FOREVER on the Zephyr backend.
    return xQueueReceive(p_layout->handle, p_item, portMAX_DELAY) == pdPASS;
}

// --- Timer ---------------------------------------------------------------

struct TimerLayout
{
    TimerUserData user_data;
    StaticTimer_t control_block;
};

static_assert(sizeof(TimerLayout) <= sizeof(TimerStorage{}.bytes),
              "hal::os::TimerStorage is too small for FreeRTOS's StaticTimer_t "
              "plus the callback/context/handle this backend stores alongside "
              "it — bump TimerStorage::bytes in os.hpp");

Timer::Timer() : m_storage{}
{
}

void Timer::init(TimerCallback callback, void* p_context)
{
    auto* const p_layout = reinterpret_cast<TimerLayout*>(m_storage.bytes);

    p_layout->user_data.callback = callback;
    p_layout->user_data.p_context = p_context;

    // Period is set for real by start_once()/start_periodic(): FreeRTOS
    // wants a nonzero tick count at creation, so this is a placeholder that
    // is always overwritten via xTimerChangePeriod() before the timer can
    // fire (a timer is never started except through start_once()/
    // start_periodic()). pvTimerID is set to &user_data at creation so
    // timer_expiry_handler() can recover it directly, no separate
    // vTimerSetTimerID() call needed.
    p_layout->user_data.handle =
        xTimerCreateStatic("eda_timer", pdMS_TO_TICKS(1U), pdFALSE, &p_layout->user_data,
                           timer_expiry_handler, &p_layout->control_block);
}

void Timer::start_once(uint32_t duration_ms)
{
    auto* const p_layout = reinterpret_cast<TimerLayout*>(m_storage.bytes);

    xTimerChangePeriod(p_layout->user_data.handle, pdMS_TO_TICKS(duration_ms), 0U);
    vTimerSetReloadMode(p_layout->user_data.handle, pdFALSE);
    xTimerStart(p_layout->user_data.handle, 0U);
}

void Timer::start_periodic(uint32_t period_ms)
{
    auto* const p_layout = reinterpret_cast<TimerLayout*>(m_storage.bytes);

    xTimerChangePeriod(p_layout->user_data.handle, pdMS_TO_TICKS(period_ms), 0U);
    vTimerSetReloadMode(p_layout->user_data.handle, pdTRUE);
    xTimerStart(p_layout->user_data.handle, 0U);
}

void Timer::stop()
{
    auto* const p_layout = reinterpret_cast<TimerLayout*>(m_storage.bytes);

    // xTimerStop() is documented as ISR-unsafe; xTimerStopFromISR() is the
    // separate entry point. hal::os::Timer::stop() is documented (os.hpp) as
    // safe to call from interrupt context, so route through the timer
    // command queue either way rather than exposing that split upward.
    // xPortIsInsideInterrupt() is an ARM Cortex-M port function (present on
    // the port this MCU would use); a different FreeRTOS port may need a
    // different way to detect ISR context here.
    if (xPortIsInsideInterrupt() == pdTRUE)
    {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xTimerStopFromISR(p_layout->user_data.handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
    else
    {
        xTimerStop(p_layout->user_data.handle, 0U);
    }
}

// --- Idle hook and misc ----------------------------------------------------

void register_idle_callback(IdleCallback callback)
{
    // Unlike the Zephyr backend, nothing further is needed here: FreeRTOS
    // has a genuine idle hook (vApplicationIdleHook() below), so there is no
    // approximation thread to start.
    s_idle_callback = callback;
}

uint32_t get_uptime_ms()
{
    return static_cast<uint32_t>(xTaskGetTickCount()) * portTICK_PERIOD_MS;
}

void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

} // namespace hal::os

/// Called by the FreeRTOS kernel's idle task. Requires
/// `configUSE_IDLE_HOOK 1` in FreeRTOSConfig.h — see the file-level comment.
/// Defined inside `hal::os` (extern "C" linkage still applies to the symbol
/// the linker sees) so it can reach `s_idle_callback` directly, the same way
/// every other function in this file does.
namespace hal::os
{
extern "C" void vApplicationIdleHook(void)
{
    if (s_idle_callback != nullptr)
    {
        s_idle_callback();
    }
}
} // namespace hal::os

/// @}
