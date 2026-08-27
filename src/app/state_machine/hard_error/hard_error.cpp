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
#include "utils/fault/fault.hpp"
#include "svc/acquisition/port.hpp"
#include "svc/comms/port.hpp"
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

    // svc::system_diagnostics owns the LEDs from here on: ERROR and ACTIVITY
    // blinking together. Setting one here would only fight it.
    utils::fault::report(utils::fault::Reason::UNRECOVERABLE);

    // Stop radiating. A device that cannot deliver what it collects should not
    // keep a receiver running and should not keep transmitting.
    eda::Port::send_event_critical(app::PortList::ACQUISITION_PORT,
                          static_cast<uint32_t>(svc::acquisition::Event::STOP_SCAN), 0);

    eda::Port::send_event_critical(app::PortList::COMMS_PORT,
                          static_cast<uint32_t>(svc::comms::Event::STOP_DISPATCH), 0);

    // No automatic reset on purpose. Rebooting out of an unrecoverable fault
    // erases what caused it. The watchdog is still fed, so the device sits here
    // blinking until somebody looks at it.
}

void HardErrorState::exit()
{
    // Never runs: this state is terminal. The fault latch does not clear
    // either, so nothing turns the blink off short of a reset.
}

void HardErrorState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    (void)event_id;
    (void)opt_data_address;

    // Terminal by design: nothing leaves this state except a reset.
}

} // namespace app

/// @}
