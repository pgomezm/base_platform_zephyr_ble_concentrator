/// @addtogroup grp_eda
/// @{
///
/// @file timer.hpp
///
/// Timer class declaration.

#pragma once

#include "hal/os/os.hpp"

#include <cstdint>

namespace eda
{
class Timer; // Forward declaration

/// Callback function type for the timer.
///
/// Ported from `deepsight-polaris-software`'s `eda::CallbackFunction`, which
/// takes the FreeRTOS `TimerHandle_t` that expired. `hal::os` has no handle
/// type with that meaning (see hal/os/os.hpp), so the callback instead
/// receives the `eda::Timer*` that expired, which is the same information:
/// enough to call get_context()/set_context() on the right timer from a
/// callback shared by more than one.
typedef void (*CallbackFunction)(Timer* p_timer);

/// @enum TimerErrorCode
/// Enum type returned by the functions.
enum class TimerErrorCode
{
    SUCCESS,
    TIMER_HANDLE_NULL,
};

/// Timer class that wraps a hal::os::Timer.
class Timer
{
public:
    /// Timer constructor
    ///
    /// @param name Timer name
    /// @param period Timer period in milliseconds
    /// @param is_periodic Flag to indicate if the timer is periodic
    /// @param callback Callback function for the timer
    Timer(const char* const name, uint32_t period, bool is_periodic, CallbackFunction callback);

    // Held by address by hal::os::Timer once initialized, so no copies or moves.
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    /// Set the context (pointer to the object)
    ///
    /// @param context Pointer to the object that will be used in the callback
    void set_context(void* context);

    /// Get the context (pointer to the object)
    ///
    /// @return the context last set by set_context(), or nullptr
    void* get_context() const;

    /// Function to start the timer
    TimerErrorCode start(void);

    /// Function to start the timer with a different period
    TimerErrorCode start(const uint32_t period);

    /// Function to stop the timer
    void stop(void);

    /// Function to start the timer from an ISR
    TimerErrorCode start_from_isr(void);

    /// Function to start the timer from an ISR with a different period
    TimerErrorCode start_from_isr(const uint32_t period);

    /// Function to stop the timer from an ISR
    void stop_from_isr(void);

private:
    /// hal::os expiry callback. Runs in the backend's timer-service context
    /// (a Zephyr ISR): recovers the eda::Timer and forwards to m_callback.
    ///
    /// @param p_context the eda::Timer* that expired, passed back verbatim
    static void expiry_trampoline(void* p_context);

    /// The underlying kernel timer
    hal::os::Timer m_timer;

    /// Callback function for the timer
    CallbackFunction m_callback;

    /// Timer name, kept for logging/debugging
    const char* m_name;

    /// Period last configured, in milliseconds
    uint32_t m_period;

    /// Whether the timer restarts itself on expiry
    bool m_is_periodic;

    /// Store the context set via set_context()/get_context()
    void* m_context;
};

/// Get the system uptime in milliseconds
///
/// @return System uptime in milliseconds since boot
uint32_t get_uptime_ms();

/// Delay the current task for a specified number of milliseconds
///
/// @param ms Number of milliseconds to delay
void delay_ms(uint32_t ms);

} // namespace eda

/// @}
