/// @addtogroup grp_app
/// @{
///
/// @file hard_error.cpp
///
/// Source file that implements the hard error state.

#include "app/state_machine/hard_error/hard_error.hpp"
#include "app/port.hpp"
#include "app/port_list.hpp"

#include "eda/port/port.hpp"
#include "hal/led/led.hpp"
#include "svc/acquisition/port.hpp"
#include "svc/comms/subsystem.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_USE(app);

namespace app
{

HardErrorState::HardErrorState(eda::StateMachine& state_machine)
    : eda::State("HARD_ERROR", &state_machine)
{
}

void HardErrorState::entry()
{
    LOG_ERROR("hard error: the device has stopped operating");

    (void)hal::led::Manager::get_instance().get_led(hal::led::LedInstances::ERROR_LED).turn_on();

    // Stop radiating. A device that cannot deliver what it collects should not
    // keep a receiver running and should not keep transmitting.
    eda::Port::send_event(app::PortList::ACQUISITION_PORT,
                          static_cast<uint32_t>(svc::acquisition::Event::STOP_SCAN), 0);

    svc::comms::stop_dispatch_timer();

    // Deliberately no automatic reset. A device that reboots itself out of an
    // unrecoverable fault erases the evidence of what went wrong; the watchdog
    // in system_diagnostics is the recovery path, and the error LED is the
    // signal to whoever is standing in front of it.
}

void HardErrorState::exit()
{
    (void)hal::led::Manager::get_instance().get_led(hal::led::LedInstances::ERROR_LED).turn_off();
}

void HardErrorState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(event_id);
    ARG_UNUSED(opt_data_address);

    // Terminal by design: nothing leaves this state except a reset.
}

} // namespace app

/// @}
