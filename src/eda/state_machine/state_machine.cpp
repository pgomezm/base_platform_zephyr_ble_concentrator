/// @addtogroup grp_eda
/// @{
///
/// @file state_machine.cpp
///
/// Source file that implements the base state machine.

#include "eda/state_machine/state_machine.hpp"
#include "utils/log/log.hpp"

namespace eda
{

StateMachine::StateMachine()
    : m_p_current_state{nullptr}
{
}

void StateMachine::init(State& initial_state)
{
    m_p_current_state = &initial_state;
    m_p_current_state->on_entry();
}

void StateMachine::transition_to(State& next_state)
{
    if (m_p_current_state == &next_state)
    {
        return;
    }

    if (m_p_current_state != nullptr)
    {
        m_p_current_state->on_exit();
    }

    LOG_MODULE_INF("state: %s -> %s",
                   (m_p_current_state != nullptr) ? m_p_current_state->get_name() : "(none)",
                   next_state.get_name());

    m_p_current_state = &next_state;
    m_p_current_state->on_entry();
}

void StateMachine::dispatch(uint32_t event_id, uint32_t opt_data)
{
    if (m_p_current_state != nullptr)
    {
        m_p_current_state->handle_event(event_id, opt_data);
    }
}

State* StateMachine::get_current_state() const
{
    return m_p_current_state;
}

} // namespace eda

/// @}
