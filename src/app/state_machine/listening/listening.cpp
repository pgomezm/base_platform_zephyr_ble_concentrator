/// @addtogroup grp_app
/// @{
///
/// @file listening.cpp
///
/// Source file that implements the listening state.

#include "app/app.hpp"
#include "app/state_machine/listening/listening.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"
#include "app/port_list.hpp"

#include "eda/port/port.hpp"
#include "svc/acquisition/port.hpp"
#include "svc/comms/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

ListeningState::ListeningState(eda::StateMachine& state_machine)
    : eda::State("LISTENING", &state_machine)
{
}

void ListeningState::entry()
{
    // The concentrator's steady state: scanning, collecting, waiting for the
    // dispatch period. In the original diagram this is IDLE, and the operation
    // running inside it is the one labelled HIRING, which reads as a
    // transcription of HEARING. Named for what it does.
    // The scan is already running: it starts in STARTUP and stops only in
    // HARD_ERROR, which is terminal. Nothing on the path into this state
    // interrupts it, so re-arming here would be a second answer to a question
    // that already has one.
    svc::comms::start_dispatch_timer();

    LOG_INF("listening for endpoint advertisements");
}

void ListeningState::exit()
{
}

void ListeningState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::DISPATCH_STARTED:
        App::get_instance().get_state_machine().transition_to(StateId::DISPATCHING);
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
