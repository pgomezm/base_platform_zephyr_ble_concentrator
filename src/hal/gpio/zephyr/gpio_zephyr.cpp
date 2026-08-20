/// @addtogroup grp_hal_gpio
/// @{
///
/// @file gpio_zephyr.cpp
///
/// Source file that implements the GPIO HAL on Zephyr's GPIO driver.

#include "hal/gpio/gpio.hpp"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hal_gpio, CONFIG_APP_LOG_LEVEL);

namespace hal::gpio
{
namespace
{

/// One GPIO pin, backed by a devicetree spec.
///
/// The callback slot exists because IGpio declares it. Nothing in this firmware
/// registers one: every GPIO here is an output. Wiring it up means adding a
/// gpio_callback and an interrupt configuration, which is deliberately not done
/// speculatively.
class Gpio : public IGpio
{
public:
    explicit Gpio(const struct gpio_dt_spec& spec) : m_spec(spec), m_callback(nullptr) {}

    GpioState read() override
    {
        return (gpio_pin_get_dt(&m_spec) > 0) ? GpioState::HIGH : GpioState::LOW;
    }

    void write(GpioState state) override
    {
        (void)gpio_pin_set_dt(&m_spec, (state == GpioState::HIGH) ? 1 : 0);
    }

    void toggle() override
    {
        (void)gpio_pin_toggle_dt(&m_spec);
    }

    void set_callback(GpioCallback callback) override
    {
        m_callback = callback;
    }

    /// Configure the pin as an output, starting inactive.
    ///
    /// @return true if the pin was configured
    bool configure_as_output()
    {
        if (!gpio_is_ready_dt(&m_spec))
        {
            LOG_ERR("GPIO not ready");
            return false;
        }

        if (gpio_pin_configure_dt(&m_spec, GPIO_OUTPUT_INACTIVE) != 0)
        {
            LOG_ERR("failed to configure GPIO as output");
            return false;
        }

        return true;
    }

private:
    const struct gpio_dt_spec m_spec;
    GpioCallback m_callback;
};

/// The GPIO manager for this board.
///
/// Every pin is configured once, in the constructor, which runs the first time
/// ManagerFactory::get_instance() is called. Bring-up order is therefore a
/// property of who asks for a GPIO first, not of a separate initialize() call
/// somebody can forget.
class Manager : public IManager
{
public:
    Manager()
        : m_heartbeat_led(GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios)),
          m_activity_led(GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios)),
          m_error_led(GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios))
    {
        (void)m_heartbeat_led.configure_as_output();
        (void)m_activity_led.configure_as_output();
        (void)m_error_led.configure_as_output();
    }

    IGpio& get_gpio(GpioInstances instance) override
    {
        switch (instance)
        {
        case GpioInstances::HEARTBEAT_LED:
            return m_heartbeat_led;
        case GpioInstances::ACTIVITY_LED:
            return m_activity_led;
        case GpioInstances::ERROR_LED:
            return m_error_led;
        default:
            // Return the first GPIO as fallback to avoid undefined behavior
            return m_heartbeat_led;
        }
    }

private:
    Gpio m_heartbeat_led;
    Gpio m_activity_led;
    Gpio m_error_led;
};

} // namespace

IManager& ManagerFactory::get_instance()
{
    static Manager instance;

    return instance;
}

} // namespace hal::gpio

/// @}
