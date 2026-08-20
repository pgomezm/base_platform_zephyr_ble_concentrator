/// @addtogroup grp_hal_led
/// @{
///
/// @file
///
/// LED instances enumeration.
/// This header file declares the LED instances of the system.

#pragma once

#include <cstdint>

namespace hal::led
{

enum class LedInstances : uint32_t
{
    HEARTBEAT_LED = 0,
    ACTIVITY_LED,
    ERROR_LED,
};

} // namespace hal::led

/// @}
