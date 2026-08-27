/// @addtogroup grp_hal_led
/// @{
///
/// @file
///
/// LED HAL implementation file.
/// This file implements the LED HAL interface.
///
/// There is no platform subdirectory under this module and there does not need
/// to be: every platform-specific line lives behind hal::gpio::IGpio, so this
/// file compiles unchanged on any backend that provides one.

#include "hal/led/led.hpp"

#include "hal/gpio/gpio.hpp"

namespace hal::led
{

Led::Led(hal::gpio::IGpio& gpio) : m_gpio(gpio), m_state(false)
{}

Error Led::turn_on()
{
    m_gpio.write(hal::gpio::GpioState::HIGH);
    m_state = true;

    return Error::OK;
}

Error Led::turn_off()
{
    m_gpio.write(hal::gpio::GpioState::LOW);
    m_state = false;

    return Error::OK;
}

Error Led::toggle()
{
    m_gpio.toggle();
    m_state = !m_state;

    return Error::OK;
}

bool Led::get_state() const
{
    return m_state;
}

Manager::Manager()
    : m_heartbeat_led(hal::gpio::ManagerFactory::get_instance().get_gpio(
          hal::gpio::GpioInstances::HEARTBEAT_LED)),
      m_activity_led(hal::gpio::ManagerFactory::get_instance().get_gpio(
          hal::gpio::GpioInstances::ACTIVITY_LED)),
      m_error_led(
          hal::gpio::ManagerFactory::get_instance().get_gpio(hal::gpio::GpioInstances::ERROR_LED))
{}

Manager& Manager::get_instance()
{
    static Manager instance;

    return instance;
}

Led& Manager::get_led(LedInstances instance)
{
    switch (instance)
    {
    case LedInstances::HEARTBEAT_LED:
        return m_heartbeat_led;
    case LedInstances::ACTIVITY_LED:
        return m_activity_led;
    case LedInstances::ERROR_LED:
        return m_error_led;
    default:
        // Return the first LED as fallback to avoid undefined behavior
        return m_heartbeat_led;
    }
}

} // namespace hal::led

/// @}
