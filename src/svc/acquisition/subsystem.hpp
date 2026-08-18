/// @defgroup grp_svc_acquisition Acquisition Service
///
/// Owns the BLE scan lifecycle and parses endpoint advertisements.
///
/// @addtogroup grp_svc_acquisition
/// @{
///
/// @file subsystem.hpp
///
/// Header file that declares the acquisition service.

#pragma once

#include "svc/acquisition/port.hpp"

#include <cstdint>

namespace svc::acquisition
{

/// Initialize the service: create its active object, register the BLE callback.
///
/// Does not start scanning; the application state machine decides when.
///
/// @return true if the service came up
bool initialize();

/// Get the port of this service, to post events to it.
///
/// @return reference to the port
Port& get_port();

/// Number of advertising reports dropped because the report pool was full.
///
/// Reported in the uplink so that a crowded room reads as "the concentrator saw
/// more than it could keep up with" instead of silently under-reporting.
///
/// @return the running count, saturating at UINT16_MAX
uint16_t get_dropped_report_count();

} // namespace svc::acquisition

/// @}
