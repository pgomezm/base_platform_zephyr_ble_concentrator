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

// Starting and stopping the dispatch timer used to be two functions here. They
// are Event::START_DISPATCH and Event::STOP_DISPATCH now: the timer belongs to
// this service, and a caller reaching in to start it ran that code in the
// caller's thread, which is the one thing the active object model exists to
// prevent. See src/svc/comms/comms.md.

} // namespace svc::comms

/// @}
