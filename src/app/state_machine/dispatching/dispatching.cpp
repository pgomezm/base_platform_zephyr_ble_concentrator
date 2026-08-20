/// @addtogroup grp_app
/// @{
///
/// @file dispatching.cpp
///
/// Source file that implements the dispatching state.

#include "app/app.hpp"
#include "app/state_machine/dispatching/dispatching.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

DispatchingState::DispatchingState(eda::StateMachine& state_machine)
    : eda::State("DISPATCHING", &state_machine)
{
}

void DispatchingState::entry()
{
    // Scanning is deliberately left running. The dispatch reads a snapshot
    // taken at the start of the cycle, so readings that arrive while the uplink
    // is in flight are simply picked up by the next cycle rather than lost.
    LOG_INF("dispatching uplink");
}

void DispatchingState::exit()
{
}

void DispatchingState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::DISPATCH_FINISHED:
        App::get_instance().get_state_machine().transition_to(StateId::LISTENING);
        break;

    case Event::SOFT_ERROR:
        App::get_instance().get_state_machine().transition_to(StateId::SOFT_ERROR);
        break;

    case Event::HARD_ERROR:
        App::get_instance().get_state_machine().transition_to(StateId::HARD_ERROR);
        break;

    default:
        break;
    }
}

} // namespace app

/// @}
