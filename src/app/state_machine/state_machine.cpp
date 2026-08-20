/// @addtogroup grp_app
/// @{
///
/// @file state_machine.cpp
///
/// Source file that implements the application state machine.

#include "app/state_machine/state_machine.hpp"

#include "app/app.hpp"

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

/// The single state machine instance. Declared before the states below: they
/// only store its address at construction (eda::State's constructor never
/// dereferences it), so which one is finished constructing first does not
/// matter, but declaring the state machine first keeps the file read in the
/// same order execution eventually uses it.
StateMachine s_state_machine;

/// The states. Statically allocated, one instance each, exactly as
/// deepsight-polaris-software expects a StateMachine's states to be owned by
/// whatever module defines the state machine, not by eda:: itself.
StartupState s_startup_state{s_state_machine};
ListeningState s_listening_state{s_state_machine};
DispatchingState s_dispatching_state{s_state_machine};
SoftErrorState s_soft_error_state{s_state_machine};
HardErrorState s_hard_error_state{s_state_machine};

} // namespace

StateMachine::StateMachine() : eda::StateMachine("app", nullptr), m_current_state_id{StateId::STARTUP}
{
}

void StateMachine::init_action()
{
    // Runs from eda::StateMachine::init(), called once every service is up
    // (see app::initialize()). This is the one place the initial state is
    // named, per the two-phase init eda::StateMachine expects: the
    // constructor above hands the base a null initial state, and this is
    // what replaces it before entry() ever runs.
    set_next_state(&s_startup_state);
    m_current_state_id = StateId::STARTUP;
}

void StateMachine::transition_to(StateId state_id)
{
    // The whole transition table, in one place. States ask for a transition;
    // they never reach into each other.
    switch (state_id)
    {
    case StateId::STARTUP:
        change_state(&s_startup_state);
        break;

    case StateId::LISTENING:
        change_state(&s_listening_state);
        break;

    case StateId::DISPATCHING:
        change_state(&s_dispatching_state);
        break;

    case StateId::SOFT_ERROR:
        change_state(&s_soft_error_state);
        break;

    case StateId::HARD_ERROR:
        change_state(&s_hard_error_state);
        break;

    default:
        LOG_ERR("transition to unknown state %u", static_cast<unsigned>(state_id));
        return;
    }

    LOG_INF("state -> %s", get_current_state_name());

    m_current_state_id = state_id;
}

StateId StateMachine::get_current_state_id() const
{
    return m_current_state_id;
}

// Defined here rather than in app.cpp because the state machine instance and
// the state objects that hold a reference to it are owned by this file, and
// their construction order is the reason they are file statics (see the note on
// s_state_machine above). App exposes it; this file owns it.
StateMachine& App::get_state_machine()
{
    return s_state_machine;
}

} // namespace app

/// @}
