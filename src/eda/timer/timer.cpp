/// @addtogroup grp_eda
/// @{
///
/// @file timer.cpp
///
/// Source file that implements the event timer.

#include "eda/timer/timer.hpp"
#include "eda/port/port.hpp"

namespace eda
{

Timer::Timer(Port& port, uint32_t event_id)
    : m_timer{}
    , m_port{port}
    , m_event_id{event_id}
{
    k_timer_init(&m_timer, Timer::expiry_handler, nullptr);
    k_timer_user_data_set(&m_timer, this);
}

void Timer::start_once(uint32_t duration_ms)
{
    k_timer_start(&m_timer, K_MSEC(duration_ms), K_NO_WAIT);
}

void Timer::start_periodic(uint32_t period_ms)
{
    k_timer_start(&m_timer, K_MSEC(period_ms), K_MSEC(period_ms));
}

void Timer::stop()
{
    k_timer_stop(&m_timer);
}

bool Timer::is_running() const
{
    // k_timer_remaining_get takes a non-const pointer, and reading the
    // remaining time does not modify observable state.
    return k_timer_remaining_get(const_cast<struct k_timer*>(&m_timer)) > 0U;
}

void Timer::expiry_handler(struct k_timer* p_timer)
{
    auto* const p_self = static_cast<Timer*>(k_timer_user_data_get(p_timer));

    if (p_self != nullptr)
    {
        // ISR context: post and return, nothing else.
        p_self->m_port.post_from_isr(p_self->m_event_id);
    }
}

} // namespace eda

/// @}
