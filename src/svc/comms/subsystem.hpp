/// @defgroup grp_svc_comms Comms Service
///
/// Builds, fragments and dispatches the uplink.
///
/// @addtogroup grp_svc_comms
/// @{
///
/// @file subsystem.hpp
///
/// Header file that declares the comms service.

#pragma once

#include "svc/comms/port.hpp"

#include <cstdint>

namespace svc::comms
{

/// Initialize the service: create its active object and its dispatch timer.
///
/// Does not join the network or start the timer; the application state machine
/// decides when.
///
/// @return true if the service came up
bool initialize();

/// Get the port of this service, to post events to it.
///
/// @return reference to the port
Port& get_port();

/// Start the periodic dispatch timer.
void start_dispatch_timer();

/// Stop the periodic dispatch timer.
void stop_dispatch_timer();

} // namespace svc::comms

/// @}
