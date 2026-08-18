/// @addtogroup grp_svc_system_diagnostics
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port of the system diagnostics service.

#include "svc/system_diagnostics/port.hpp"
#include "svc/system_diagnostics/subsystem.hpp"
#include "app/port_list.hpp"
#include "eda/active_object/active_object.hpp"

namespace svc::system_diagnostics
{

Port::Port(eda::ActiveObject& active_object)
    : eda::Port{active_object, app::PortList::SYSTEM_DIAGNOSTICS_PORT}
{
}

} // namespace svc::system_diagnostics

/// @}
