/// @defgroup grp_svc_system_diagnostics System Diagnostics Service
///
/// Heartbeat, watchdog and health checks for the concentrator itself.
///
/// @addtogroup grp_svc_system_diagnostics
/// @{
///
/// @file subsystem.hpp
///
/// Header file that declares the system diagnostics service.

#pragma once

#include "svc/system_diagnostics/port.hpp"

namespace svc::system_diagnostics
{

/// Initialize the service and start its heartbeat.
///
/// @return true if the service came up
bool initialize();

/// Get the port of this service, to post events to it.
///
/// @return reference to the port
Port& get_port();

} // namespace svc::system_diagnostics

/// @}
