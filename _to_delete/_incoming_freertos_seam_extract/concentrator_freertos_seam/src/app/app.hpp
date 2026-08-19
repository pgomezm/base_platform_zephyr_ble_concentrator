/// @defgroup grp_app Application
///
/// The high-level logic of the concentrator.
///
/// @addtogroup grp_app
/// @{
///
/// @file app.hpp
///
/// Header file that declares the application.

#pragma once

#include "app/port.hpp"

namespace app
{

/// Initialize every layer and enter the state machine.
///
/// Called once from main(). Brings up the HAL, then the services, then the
/// application state machine, in that order, and reports upward if any of them
/// fails rather than continuing into a half-initialized system.
///
/// @return true if the firmware came up
bool initialize();

/// Get the port of the application, to post events to it.
///
/// @return reference to the port
Port& get_port();

} // namespace app

/// @}
