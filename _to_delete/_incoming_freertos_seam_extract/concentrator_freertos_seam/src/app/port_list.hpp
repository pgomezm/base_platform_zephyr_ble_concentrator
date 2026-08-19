/// @addtogroup grp_app
/// @{
///
/// @file port_list.hpp
///
/// Header file that declares the enumeration of the ports used by the application.

#pragma once

#include <cstdint>

namespace app
{

/// Identifier of every port in the firmware.
///
/// One entry per module that owns an eda::Port. Used for logging and for
/// asserting that an event reached the port it was addressed to.
enum class PortList : uint8_t
{
    INVALID_PORT,
    APP_PORT,
    ACQUISITION_PORT,
    COMMS_PORT,
    SYSTEM_DIAGNOSTICS_PORT,

    /// Not a port. Sentinel giving the size of the port registry, so
    /// `eda::Port` can size its static array from this enum instead of a
    /// magic number that has to be kept in sync by hand.
    PORT_COUNT,
};

} // namespace app

/// @}
