/// @defgroup grp_hal_system System HAL
///
/// Hardware Abstraction Layer for system-level operations.
///
/// @addtogroup grp_hal_system
/// @{
///
/// @file system.hpp
///
/// Header file that declares the system HAL interface.

#pragma once

#include <cstdint>

namespace hal::system
{

/// Why the device last reset.
///
/// Reported in the first uplink after boot, so whatever consumes the uplinks
/// knows the device table started empty rather than inferring a gap.
enum class ResetReason : uint8_t
{
    UNKNOWN,
    POWER_ON,
    PIN_RESET,
    SOFTWARE,
    WATCHDOG,
    BROWNOUT,
};

/// Read the reason for the last reset.
///
/// @return the reset reason
ResetReason get_reset_reason();

/// Uptime since boot, in seconds.
///
/// This is the device's own clock. It is not wall-clock time: the concentrator
/// holds no persistent state and has no RTC source, so every timestamp it
/// reports is relative to its own boot.
///
/// @return seconds since boot
uint32_t get_uptime_seconds();

/// Reset the device.
void reset();

} // namespace hal::system

/// @}
