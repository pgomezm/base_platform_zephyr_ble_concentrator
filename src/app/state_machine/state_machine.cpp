/// @addtogroup grp_app
/// @{
///
/// @file state_machine.cpp
///
/// Source file that implements the application state machine.

#include "app/state_machine/state_machine.hpp"

#include "app/state_machine/dispatching/dispatching.hpp"
#include "app/state_machine/hard_error/hard_error.hpp"
#include "app/state_machine/listening/listening.hpp"
#include "app/state_machine/soft_error/soft_error.hpp"
#include "app/state_machine/startup/startup.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{
namespace
{

/// The states. Statically allocated, one instance each.
StartupState s_startup_state;
ListeningState s_listening_state;
DispatchingState s_dispatching_state;
SoftErrorState s_soft_error_state;
HardErrorState s_hard_error_state;

/// The single state machine instance.
StateMachine s_state_machine;

} // namespace

StateMachine::StateMachine()
    : m_state_machine{}
    , m_current_state_id{StateId::STARTUP}
{
}

void StateMachine::initialize()
{
    m_current_state_id = StateId::STARTUP;
    m_state_machine.init(s_startup_state);
}

void StateMachine::dispatch(uint32_t event_id, uint32_t opt_data)
{
    m_state_machine.dispatch(event_id, opt_data);
}

void StateMachine::transition_to(StateId state_id)
{
    // The whole transition table, in one place. States ask for a transition;
    // they never reach into each other.
    switch (state_id)
    {
    case StateId::STARTUP:
        m_state_machine.transition_to(s_startup_state);
        break;

    case StateId::LISTENING:
        m_state_machine.transition_to(s_listening_state);
        break;

    case StateId::DISPATCHING:
        m_state_machine.transition_to(s_dispatching_state);
        break;

    case StateId::SOFT_ERROR:
        m_state_machine.transition_to(s_soft_error_state);
        break;

    case StateId::HARD_ERROR:
        m_state_machine.transition_to(s_hard_error_state);
        break;

    default:
        LOG_ERR("transition to unknown state %u", static_cast<unsigned>(state_id));
        return;
    }

    m_current_state_id = state_id;
}

StateId StateMachine::get_current_state_id() const
{
    return m_current_state_id;
}

StateMachine& get_state_machine()
{
    return s_state_machine;
}

} // namespace app

/// @}
