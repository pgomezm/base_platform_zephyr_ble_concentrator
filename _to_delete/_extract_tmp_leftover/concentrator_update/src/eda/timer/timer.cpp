/// @addtogroup grp_eda
/// @{
///
/// @file timer.cpp
///
/// Source file that implements the timer class.

#include "eda/timer/timer.hpp"

namespace eda
{

Timer::Timer(const char* const name, uint32_t period, bool is_periodic, CallbackFunction callback)
    : m_timer{}, m_callback(callback), m_name(name), m_period(period),
      m_is_periodic(is_periodic), m_context(nullptr)
{
    m_timer.init(&Timer::expiry_trampoline, this);
}

void Timer::set_context(void* context)
{
    m_context = context;
}

void* Timer::get_context() const
{
    return m_context;
}

TimerErrorCode Timer::start(void)
{
    return start(m_period);
}

TimerErrorCode Timer::start(const uint32_t period)
{
    m_period = period;

    if (m_is_periodic)
    {
        m_timer.start_periodic(m_period);
    }
    else
    {
        m_timer.start_once(m_period);
    }

    // hal::os::Timer has no failure mode to report: its storage is a fixed
    // member of this object, never allocated, so there is no null-handle
    // case the way a FreeRTOS xTimerCreateStatic() call could fail and
    // return NULL. The error code stays part of the interface for parity
    // with deepsight-altair-software's Timer, and for whatever hal::os
    // backend comes next that might have a real failure mode to report.
    return TimerErrorCode::SUCCESS;
}

void Timer::stop(void)
{
    m_timer.stop();
}

TimerErrorCode Timer::start_from_isr(void)
{
    // On Zephyr, k_timer_start() is already ISR-safe, so this is the same
    // call as start(). The separate entry point documents the calling
    // context, matching deepsight-altair-software's Timer.
    return start();
}

TimerErrorCode Timer::start_from_isr(const uint32_t period)
{
    return start(period);
}

void Timer::stop_from_isr(void)
{
    stop();
}

void Timer::expiry_trampoline(void* p_context)
{
    auto* const p_self = static_cast<Timer*>(p_context);

    if ((p_self != nullptr) && (p_self->m_callback != nullptr))
    {
        // ISR context: hand off and return, nothing else.
        p_self->m_callback(p_self);
    }
}

uint32_t get_uptime_ms()
{
    return hal::os::get_uptime_ms();
}

void delay_ms(uint32_t ms)
{
    hal::os::delay_ms(ms);
}

} // namespace eda

/// @}
