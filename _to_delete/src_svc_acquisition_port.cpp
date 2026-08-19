/// @addtogroup grp_svc_acquisition
/// @{
///
/// @file port.cpp
///
/// Source file that implements the port of the acquisition service.

#include "svc/acquisition/port.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "app/port_list.hpp"
#include "eda/active_object/active_object.hpp"

namespace svc::acquisition
{

Port::Port(eda::ActiveObject& active_object)
    : eda::Port{active_object, app::PortList::ACQUISITION_PORT}
{
}

} // namespace svc::acquisition

/// @}
