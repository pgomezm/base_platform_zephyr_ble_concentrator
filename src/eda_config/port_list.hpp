/// @addtogroup grp_eda_config
/// @{
///
/// @file port_list.hpp
///
/// Header file that declares the ports this project gives eda:: to work with.

#pragma once

#include <cstdint>

namespace eda_config
{

/// Identifier of every port in the firmware.
///
/// One entry per module that owns an eda::Port. Used for logging and for
/// asserting that an event reached the port it was addressed to.
///
/// This list is the project's, but the header path and the type name are
/// eda::'s: it includes "eda_config/port_list.hpp" and expects to find
/// eda_config::PortList in it. See eda_config.md.
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

} // namespace eda_config

/// @}
