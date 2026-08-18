/// @addtogroup grp_svc_comms
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port of the comms service.

#include "svc/comms/port.hpp"
#include "svc/comms/subsystem.hpp"
#include "app/port_list.hpp"
#include "eda/active_object/active_object.hpp"

namespace svc::comms
{

Port::Port(eda::ActiveObject& active_object)
    : eda::Port{active_object, app::PortList::COMMS_PORT}
{
}

} // namespace svc::comms

/// @}
