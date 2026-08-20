/// @defgroup grp_hal_led LED HAL
///
/// Hardware Abstraction Layer for the status LEDs.
///
/// @addtogroup grp_hal_led
/// @{
///
/// @file
///
/// LED HAL header file.
/// This header file contains the declaration of the LED HAL classes and interfaces.

#pragma once

#include "led_instances.hpp"

#include "hal/gpio/gpio.hpp"

#include <cstdint>

namespace hal::led
{

enum class Error
{
    OK = 0,
    ERROR = 1
};

/// LED class that wraps a GPIO pin
class Led
{
public:
    /// Constructor
    ///
    /// @param gpio Reference to the GPIO instance controlling this LED
    explicit Led(hal::gpio::IGpio& gpio);

    /// Turn the LED on
    ///
    /// @return Error::OK if successful, Error::ERROR otherwise
    Error turn_on();

    /// Turn the LED off
    ///
    /// @return Error::OK if successful, Error::ERROR otherwise
    Error turn_off();

    /// Toggle the LED state
    ///
    /// @return Error::OK if successful, Error::ERROR otherwise
    Error toggle();

    /// Get the current state of the LED
    ///
    /// @return true if LED is on, false if LED is off
    bool get_state() const;

private:
    hal::gpio::IGpio& m_gpio;
    bool m_state;
};

/// Manager class for LED instances
class Manager
{
public:
    /// Get the singleton instance of the LED Manager
    ///
    /// @return The singleton instance of the LED Manager
    static Manager& get_instance();

    /// Get an LED instance by its enum value
    ///
    /// @param instance The LED instance to get
    /// @return Reference to the LED instance
    Led& get_led(LedInstances instance);

private:
    Manager();
    ~Manager() = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    Led m_heartbeat_led;
    Led m_activity_led;
    Led m_error_led;
};

} // namespace hal::led

/// @}
