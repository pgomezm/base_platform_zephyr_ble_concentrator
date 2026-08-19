/// @addtogroup grp_hal_led
/// @{
///
/// @file led_zephyr.cpp
///
/// Source file that implements the LED HAL on Zephyr's GPIO driver.

#include "hal/led/led.hpp"

#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hal_led, CONFIG_APP_LOG_LEVEL);

namespace hal::led
{
namespace
{

/// The board's LEDs, from the standard `led0`..`led2` devicetree aliases.
const struct gpio_dt_spec s_leds[] = {
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios), // HEARTBEAT
    GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios), // ACTIVITY
    GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios), // ERROR
};

/// Resolve an Id to its GPIO spec.
///
/// @param id which LED
/// @return pointer to the spec, or nullptr if the id is out of range
const struct gpio_dt_spec* get_spec(Id id)
{
    const auto index = static_cast<size_t>(id);

    if (index >= ARRAY_SIZE(s_leds))
    {
        return nullptr;
    }

    return &s_leds[index];
}

} // namespace

bool initialize()
{
    bool all_ready = true;

    for (const auto& led : s_leds)
    {
        if (!gpio_is_ready_dt(&led))
        {
            LOG_ERR("LED GPIO not ready");
            all_ready = false;
            continue;
        }

        if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0)
        {
            LOG_ERR("failed to configure LED GPIO");
            all_ready = false;
        }
    }

    return all_ready;
}

void set_on(Id id)
{
    const auto* const p_spec = get_spec(id);

    if (p_spec != nullptr)
    {
        (void)gpio_pin_set_dt(p_spec, 1);
    }
}

void set_off(Id id)
{
    const auto* const p_spec = get_spec(id);

    if (p_spec != nullptr)
    {
        (void)gpio_pin_set_dt(p_spec, 0);
    }
}

void toggle(Id id)
{
    const auto* const p_spec = get_spec(id);

    if (p_spec != nullptr)
    {
        (void)gpio_pin_toggle_dt(p_spec);
    }
}

} // namespace hal::led

/// @}
