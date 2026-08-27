/// @addtogroup grp_hal_gpio
/// @{
///
/// @file gpio_zephyr.cpp
///
/// Source file that implements the GPIO HAL on Zephyr's GPIO driver.

#include "hal/gpio/gpio.hpp"
#include "utils/log/log.hpp"

#include <zephyr/drivers/gpio.h>

LOG_MODULE_DEFINE(hal_gpio);

namespace hal::gpio
{
namespace
{

/// The spec a Gpio is given when the board has no such pin.
///
/// A zeroed spec has a null `port`, which is what is_present() tests.
constexpr struct gpio_dt_spec ABSENT_PIN = {};

/// Fetch a pin from a devicetree alias, or ABSENT_PIN if the board has none.
///
/// The three LEDs this firmware drives are diagnostics, and not every board has
/// three of them - the ESP32-S3-DevKitC-1 has none at all, only an addressable
/// RGB LED that is not a GPIO. Requiring them would mean a board without LEDs
/// is a board this firmware refuses to compile for, which is the wrong
/// trade for a status light.
#define GPIO_FROM_ALIAS_OR_ABSENT(alias) GPIO_DT_SPEC_GET_OR(alias, gpios, ABSENT_PIN)

/// One GPIO pin, backed by a devicetree spec.
///
/// The callback slot exists because IGpio declares it. Nothing in this firmware
/// registers one: every GPIO here is an output. Wiring it up means adding a
/// gpio_callback and an interrupt configuration, which is deliberately not done
/// speculatively.
class Gpio : public IGpio
{
public:
    explicit Gpio(const struct gpio_dt_spec& spec) : m_spec(spec), m_callback(nullptr)
    {}

    /// Whether the board actually has this pin.
    ///
    /// False when the devicetree carries no alias for it. Every operation below
    /// is then a no-op: passing a null port to gpio_pin_set_dt() would fault,
    /// and a caller should not have to ask before turning a status LED on.
    ///
    /// @return true if there is a pin behind this object
    bool is_present() const
    {
        return m_spec.port != nullptr;
    }

    GpioState read() override
    {
        if (!is_present())
        {
            return GpioState::LOW;
        }

        return (gpio_pin_get_dt(&m_spec) > 0) ? GpioState::HIGH : GpioState::LOW;
    }

    void write(GpioState state) override
    {
        if (!is_present())
        {
            return;
        }

        (void)gpio_pin_set_dt(&m_spec, (state == GpioState::HIGH) ? 1 : 0);
    }

    void toggle() override
    {
        if (!is_present())
        {
            return;
        }

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
        if (!is_present())
        {
            return false;
        }

        if (!gpio_is_ready_dt(&m_spec))
        {
            LOG_ERROR("GPIO not ready");
            return false;
        }

        if (gpio_pin_configure_dt(&m_spec, GPIO_OUTPUT_INACTIVE) != 0)
        {
            LOG_ERROR("failed to configure GPIO as output");
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
        : m_heartbeat_led(GPIO_FROM_ALIAS_OR_ABSENT(DT_ALIAS(led0))),
          m_activity_led(GPIO_FROM_ALIAS_OR_ABSENT(DT_ALIAS(led1))),
          m_error_led(GPIO_FROM_ALIAS_OR_ABSENT(DT_ALIAS(led2)))
    {
        (void)m_heartbeat_led.configure_as_output();
        (void)m_activity_led.configure_as_output();
        (void)m_error_led.configure_as_output();

        // Said once, at bring-up, rather than discovered later by someone
        // staring at a board wondering why the error LED never lights. A board
        // with no LEDs is a board with fewer diagnostics, not a fault.
        LOG_INFO("status LEDs: heartbeat %s, activity %s, error %s",
                 m_heartbeat_led.is_present() ? "yes" : "absent",
                 m_activity_led.is_present() ? "yes" : "absent",
                 m_error_led.is_present() ? "yes" : "absent");
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
