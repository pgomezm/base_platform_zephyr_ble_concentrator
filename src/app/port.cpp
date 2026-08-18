/// @addtogroup grp_app
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port of the application.

#include "app/port.hpp"
#include "app/port_list.hpp"
#include "app/state_machine/state_machine.hpp"

#include "eda/active_object/active_object.hpp"

namespace app
{

Port::Port(eda::ActiveObject& active_object)
    : eda::Port{active_object, PortList::APP_PORT}
{
}

void Port::handle_event(uint32_t event_id, uint32_t opt_data)
{
    // The application makes no decisions of its own: the state machine is the
    // only thing that decides what an event means in the current state.
    get_state_machine().dispatch(event_id, opt_data);
}

} // namespace app

/// @}
