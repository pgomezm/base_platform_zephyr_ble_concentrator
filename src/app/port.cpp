/// @addtogroup grp_app
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port of the application.

#include "app/app.hpp"
#include "app/port.hpp"
#include "app/state_machine/state_machine.hpp"

namespace app
{

void Port::execute_event(uint32_t event_id, uint32_t opt_data_address)
{
    // The application makes no decisions of its own: the state machine is the
    // only thing that decides what an event means in the current state.
    App::get_instance().get_state_machine().dispatch_event(event_id, opt_data_address);
}

} // namespace app

/// @}
