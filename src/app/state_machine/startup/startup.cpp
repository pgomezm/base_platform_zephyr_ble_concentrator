/// @addtogroup grp_app
/// @{
///
/// @file startup.cpp
///
/// Source file that implements the startup state.

#include "app/app.hpp"
#include "app/state_machine/startup/startup.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"
#include "app/port_list.hpp"

#include "eda/port/port.hpp"
#include "hal/system/system.hpp"
#include "svc/comms/port.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

StartupState::StartupState(eda::StateMachine& state_machine) : eda::State("STARTUP", &state_machine)
{
}

void StartupState::entry()
{
    const hal::system::ResetReason reason = hal::system::get_reset_reason();

    LOG_INF("startup, reset reason %u", static_cast<unsigned>(reason));

    // Services are already initialized by this point: this state waits for the
    // network, which is the only part of coming up that can fail slowly.
    eda::Port::send_event(app::PortList::COMMS_PORT,
                          static_cast<uint32_t>(svc::comms::Event::JOIN_NETWORK), 0);
}

void StartupState::exit()
{
}

void StartupState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::NETWORK_JOINED:
        App::get_instance().get_state_machine().transition_to(StateId::LISTENING);
        break;

    case Event::NETWORK_JOIN_FAILED:
        // Recoverable: the network may simply not be reachable yet. The soft
        // error state retries rather than giving up on the first attempt.
        LOG_WRN("network join failed");
        App::get_instance().get_state_machine().transition_to(StateId::SOFT_ERROR);
        break;

    case Event::SERVICES_FAILED:
        // A service that will not initialize is not going to start working on
        // its own, so this one does not retry.
        LOG_ERR("a service failed to initialize");
        App::get_instance().get_state_machine().transition_to(StateId::HARD_ERROR);
        break;

    default:
        break;
    }
}

} // namespace app

/// @}
