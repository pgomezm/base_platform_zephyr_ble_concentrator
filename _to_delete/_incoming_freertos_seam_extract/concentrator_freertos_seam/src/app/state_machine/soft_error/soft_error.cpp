/// @addtogroup grp_app
/// @{
///
/// @file soft_error.cpp
///
/// Source file that implements the soft error state.

#include "app/state_machine/soft_error/soft_error.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/app_config.hpp"
#include "app/port.hpp"
#include "app/port_list.hpp"

#include "eda/port/port.hpp"
#include "hal/led/led.hpp"
#include "svc/comms/port.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{
namespace
{

/// Consecutive soft errors without a successful recovery.
uint8_t s_consecutive_errors = 0U;

} // namespace

SoftErrorState::SoftErrorState(eda::StateMachine& state_machine)
    : eda::State("SOFT_ERROR", &state_machine)
{
}

void SoftErrorState::entry()
{
    hal::led::set_on(hal::led::Id::ERROR);

    ++s_consecutive_errors;

    LOG_WRN("soft error %u of %u", s_consecutive_errors, app::k_max_consecutive_soft_errors);

    if (s_consecutive_errors >= app::k_max_consecutive_soft_errors)
    {
        // Retrying forever would leave a broken device looking busy. After
        // enough failures it stops and says so.
        get_state_machine().transition_to(StateId::HARD_ERROR);
        return;
    }

    // Retrying immediately would burn power without changing anything: whatever
    // failed needs time to become available again.
    eda::Port::send_event(app::PortList::COMMS_PORT,
                          static_cast<uint32_t>(svc::comms::Event::JOIN_NETWORK), 0);
}

void SoftErrorState::exit()
{
    hal::led::set_off(hal::led::Id::ERROR);
}

void SoftErrorState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::NETWORK_JOINED:
        s_consecutive_errors = 0U;
        get_state_machine().transition_to(StateId::LISTENING);
        break;

    case Event::HARD_ERROR:
        get_state_machine().transition_to(StateId::HARD_ERROR);
        break;

    default:
        break;
    }
}

} // namespace app

/// @}
