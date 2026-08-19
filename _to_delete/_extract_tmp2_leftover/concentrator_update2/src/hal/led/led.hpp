/// @defgroup grp_hal_led LED HAL
///
/// Hardware Abstraction Layer for the status LEDs.
///
/// @addtogroup grp_hal_led
/// @{
///
/// @file led.hpp
///
/// Header file that declares the LED HAL interface.

#pragma once

#include <cstdint>

namespace hal::led
{

/// The LEDs available on the board.
///
/// Named by meaning, not by position, so the mapping to a physical LED lives in
/// one place in the implementation.
enum class Id : uint8_t
{
    /// Toggled by system_diagnostics to show the firmware is alive.
    HEARTBEAT,

    /// Lit while an advertising report is being processed.
    ACTIVITY,

    /// Lit while the device is in an error state.
    ERROR,
};

/// Initialize the LEDs.
///
/// @return true if every LED was configured
bool initialize();

/// Turn an LED on.
///
/// @param id which LED
void set_on(Id id);

/// Turn an LED off.
///
/// @param id which LED
void set_off(Id id);

/// Toggle an LED.
///
/// @param id which LED
void toggle(Id id);

} // namespace hal::led

/// @}
