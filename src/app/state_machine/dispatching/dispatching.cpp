/// @addtogroup grp_app
/// @{
///
/// @file dispatching.cpp
///
/// Source file that implements the dispatching state.

#include "app/state_machine/dispatching/dispatching.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

void DispatchingState::on_entry()
{
    // Scanning is deliberately left running. The dispatch reads a snapshot
    // taken at the start of the cycle, so readings that arrive while the uplink
    // is in flight are simply picked up by the next cycle rather than lost.
    LOG_INF("dispatching uplink");
}

void DispatchingState::on_exit()
{
}

void DispatchingState::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(opt_data);

    switch (static_cast<Event>(event_id))
    {
    case Event::DISPATCH_FINISHED:
        get_state_machine().transition_to(StateId::LISTENING);
        break;

    case Event::SOFT_ERROR:
        get_state_machine().transition_to(StateId::SOFT_ERROR);
        break;

    case Event::HARD_ERROR:
        get_state_machine().transition_to(StateId::HARD_ERROR);
        break;

    default:
        break;
    }
}

const char* DispatchingState::get_name() const
{
    return "DISPATCHING";
}

} // namespace app

/// @}
