/// @addtogroup grp_hal_gpio
/// @{
///
/// @file
///
/// GPIO instances enumeration.
/// This header file declares the GPIO instances of the system.

#pragma once

#include <cstdint>

namespace hal::gpio
{

enum class GpioInstances : uint32_t
{
    HEARTBEAT_LED = 0,
    ACTIVITY_LED,
    ERROR_LED,
};

} // namespace hal::gpio

/// @}
