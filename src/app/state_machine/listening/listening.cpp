/// @addtogroup grp_app
/// @{
///
/// @file listening.cpp
///
/// Source file that implements the listening state.

#include "app/state_machine/listening/listening.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"

#include "svc/acquisition/subsystem.hpp"
#include "svc/comms/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

void ListeningState::on_entry()
{
    // The concentrator's steady state: scanning, collecting, waiting for the
    // dispatch period. In the original diagram this is IDLE, and the operation
    // running inside it is the one labelled HIRING, which reads as a
    // transcription of HEARING. Named for what it does.
    (void)svc::acquisition::get_port().post(
        static_cast<uint32_t>(svc::acquisition::Event::START_SCAN));

    svc::comms::start_dispatch_timer();

    LOG_INF("listening for endpoint advertisements");
}

void ListeningState::on_exit()
{
}

void ListeningState::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(opt_data);

    switch (static_cast<Event>(event_id))
    {
    case Event::DISPATCH_STARTED:
        get_state_machine().transition_to(StateId::DISPATCHING);
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

const char* ListeningState::get_name() const
{
    return "LISTENING";
}

} // namespace app

/// @}
