/// @addtogroup grp_app
/// @{
///
/// @file hard_error.cpp
///
/// Source file that implements the hard error state.

#include "app/state_machine/hard_error/hard_error.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/port.hpp"

#include "hal/led/led.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/comms/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_APP_LOG_LEVEL);

namespace app
{

void HardErrorState::on_entry()
{
    LOG_ERR("hard error: the device has stopped operating");

    hal::led::set_on(hal::led::Id::ERROR);

    // Stop radiating. A device that cannot deliver what it collects should not
    // keep a receiver running and should not keep transmitting.
    (void)svc::acquisition::get_port().post(
        static_cast<uint32_t>(svc::acquisition::Event::STOP_SCAN));

    svc::comms::stop_dispatch_timer();

    // Deliberately no automatic reset. A device that reboots itself out of an
    // unrecoverable fault erases the evidence of what went wrong; the watchdog
    // in system_diagnostics is the recovery path, and the error LED is the
    // signal to whoever is standing in front of it.
}

void HardErrorState::on_exit()
{
    hal::led::set_off(hal::led::Id::ERROR);
}

void HardErrorState::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(event_id);
    ARG_UNUSED(opt_data);

    // Terminal by design: nothing leaves this state except a reset.
}

const char* HardErrorState::get_name() const
{
    return "HARD_ERROR";
}

} // namespace app

/// @}
