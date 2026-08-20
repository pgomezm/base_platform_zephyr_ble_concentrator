/// @defgroup grp_hal_gpio GPIO HAL
///
/// Hardware Abstraction Layer for the board's GPIO pins.
///
/// @addtogroup grp_hal_gpio
/// @{
///
/// @file
///
/// GPIO HAL header file.
/// This header file contains the declaration of the GPIO HAL classes and interfaces.

#pragma once

#include "gpio_instances.hpp"

#include <cstdint>

namespace hal::gpio
{

enum class GpioState : uint8_t
{
    LOW = 0,
    HIGH = 1
};

using GpioCallback = void (*)();

class IGpio
{
public:
    /// Read the current state of the GPIO pin
    ///
    /// @return The current state of the GPIO pin
    virtual GpioState read() = 0;

    /// Write a new state to the GPIO pin
    ///
    /// @param state The new state to write to the GPIO pin
    virtual void write(GpioState state) = 0;

    /// Toggle the state of the GPIO pin
    virtual void toggle() = 0;

    /// Set a callback function to be called on GPIO pin state changes
    ///
    /// @param callback The callback function to set
    virtual void set_callback(GpioCallback callback) = 0;

    /// Virtual destructor
    virtual ~IGpio() = default;
};

class IManager
{
public:
    /// Get a GPIO instance by its name
    ///
    /// @param instance The instance of the GPIO
    /// @return A reference to the GPIO instance
    virtual IGpio& get_gpio(GpioInstances instance) = 0;

    /// Virtual destructor
    virtual ~IManager() = default;
};

class ManagerFactory
{
public:
    /// Get the singleton instance of the GPIO Manager Factory class
    ///
    /// @return The singleton instance of the GPIO Manager Factory
    static IManager& get_instance();
};

} // namespace hal::gpio

/// @}
