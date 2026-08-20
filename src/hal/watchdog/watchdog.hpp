/// @defgroup grp_hal_watchdog Watchdog HAL
///
/// Hardware Abstraction Layer for the watchdog timer.
///
/// @addtogroup grp_hal_watchdog
/// @{
///
/// @file
///
/// Watchdog HAL header file.
/// This header file contains the declaration of the Watchdog HAL classes and interfaces.

#pragma once

#include <cstdint>

namespace hal::watchdog
{

/// Enum representing possible watchdog errors
enum class WatchdogError : uint32_t
{
    /// Indicates that the operation was successful
    NO_ERROR,

    /// Indicates a general hardware failure
    HARDWARE_ERROR,

    /// Indicates invalid timeout parameter
    INVALID_TIMEOUT,

    /// Indicates watchdog is already running (cannot reinitialize)
    ALREADY_RUNNING,
};

/// Interface for Watchdog operations
class IWatchdog
{
public:
    /// Minimum supported timeout in milliseconds
    static constexpr uint32_t MIN_TIMEOUT_MS = 500;

    /// Maximum supported timeout in milliseconds
    static constexpr uint32_t MAX_TIMEOUT_MS = 32000;

    /// Set/update the watchdog timeout period
    ///
    /// @param timeout_ms Timeout period in milliseconds
    /// @return WatchdogError indicating success or failure
    virtual WatchdogError set_timeout(uint32_t timeout_ms) = 0;

    /// Refresh the watchdog counter (feed the dog)
    ///
    /// Called from svc::system_diagnostics only. A module that refreshes the
    /// watchdog from its own thread defeats the point: the watchdog would then
    /// prove that one thread is alive, not that the firmware is.
    ///
    /// @return WatchdogError indicating success or failure
    virtual WatchdogError refresh() = 0;

    /// Get current configured timeout in milliseconds
    ///
    /// @return Timeout in milliseconds
    virtual uint32_t get_timeout() const = 0;

    /// Virtual destructor
    virtual ~IWatchdog() = default;
};

/// Factory class for Watchdog management
class WatchdogFactory
{
public:
    /// Get the singleton instance of the Watchdog
    ///
    /// @return Reference to the Watchdog instance
    static IWatchdog& get_instance();
};

} // namespace hal::watchdog

/// @}
