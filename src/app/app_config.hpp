/// @addtogroup grp_app
/// @{
///
/// @file app_config.hpp
///
/// Header file that declares configuration options for the `app` module.

#pragma once

#include "config.hpp"

#include <cstdint>

namespace app
{

/// Delay before the first join retry after a soft error, in milliseconds.
///
/// Each further consecutive failure doubles it, up to
/// @ref SOFT_ERROR_RETRY_MAX_MS.
constexpr uint32_t SOFT_ERROR_RETRY_MS = 30U * 1000U;

/// Longest the device waits between join retries, in milliseconds.
///
/// The retry never stops: a device that cannot reach its server is almost
/// always looking at a router that rebooted or an access point out of range,
/// and both come back on their own. What the cap decides is how long an outage
/// lasts beyond its end, not whether the device recovers.
constexpr uint32_t SOFT_ERROR_RETRY_MAX_MS = 15U * 60U * 1000U;

/// Watchdog timeout, in milliseconds.
///
/// eda::IdleHook feeds it, continuously, from idle. So what this number says is
/// how long the system may go without reaching idle at all before the device is
/// reset. Ten seconds is long enough that a burst of work is not mistaken for a
/// hang, and short enough that a hang is not mistaken for a working device.
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 10U * 1000U;

} // namespace app

/// @}
