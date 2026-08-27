/// @addtogroup grp_eda
/// @{
///
/// @file state_machine.hpp
///
/// Source file that implements the state machine class.

#include "eda/state_machine/state_machine.hpp"

namespace eda
{
void State::entry()
{}

void State::exit()
{}

void StateMachine::init()
{
    init_action();
    if (next_state_is_set())
    {
        m_previous_state = m_next_state;
        m_current_state = m_next_state;
        m_next_state = nullptr;
    }
    // If previous
    // Define whether asserts are necessary here
    m_current_state->entry();
}

void StateMachine::init_action()
{}

void StateMachine::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    if (m_current_state != nullptr)
    {
        m_current_state->dispatch_event(event_id, opt_data_address);
    }
}

void StateMachine::change_state(State* new_state)
{
    m_current_state->exit();
    m_previous_state = m_current_state;
    m_current_state = new_state;
    m_current_state->entry();
}

State* StateMachine::get_last_state()
{
    return m_previous_state;
}

void StateMachine::return_to_last_state()
{
    State* current_state;
    m_current_state->exit();
    current_state = m_current_state;
    m_current_state = m_previous_state;
    m_previous_state = current_state;
    m_current_state->entry();
}

void StateMachine::change_to_next_state()
{
    m_current_state->exit();
    m_previous_state = m_current_state;
    m_current_state = m_next_state;
    m_next_state = nullptr;
    m_current_state->entry();
}

void StateMachine::set_next_state(State* next_state)
{
    m_next_state = next_state;
}

bool StateMachine::next_state_is_set()
{
    return static_cast<bool>(m_next_state);
}

const char* StateMachine::get_current_state_name() const
{
    return m_current_state->m_name;
}
}

/// @}
