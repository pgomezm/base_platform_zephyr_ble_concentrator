/// @defgroup grp_hal_watchdog Watchdog HAL
///
/// Hardware Abstraction Layer for the watchdog timer.
///
/// @addtogroup grp_hal_watchdog
/// @{
///
/// @file watchdog.hpp
///
/// Header file that declares the watchdog HAL interface.

#pragma once

#include <cstdint>

namespace hal::watchdog
{

/// Initialize and start the watchdog.
///
/// @param timeout_ms time without a feed that triggers a reset
/// @return true if the watchdog started
bool initialize(uint32_t timeout_ms);

/// Feed the watchdog.
///
/// Called from system_diagnostics only. A module that feeds the watchdog from
/// its own thread defeats the point: the watchdog would then prove that one
/// thread is alive, not that the firmware is.
void feed();

} // namespace hal::watchdog

/// @}
