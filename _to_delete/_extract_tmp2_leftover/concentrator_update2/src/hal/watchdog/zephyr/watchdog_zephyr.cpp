/// @addtogroup grp_hal_watchdog
/// @{
///
/// @file watchdog_zephyr.cpp
///
/// Source file that implements the watchdog HAL on Zephyr's watchdog driver.

#include "hal/watchdog/watchdog.hpp"

#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(hal_watchdog, CONFIG_APP_LOG_LEVEL);

namespace hal::watchdog
{
namespace
{

/// The watchdog device from the devicetree.
const struct device* const s_p_watchdog_device = DEVICE_DT_GET(DT_ALIAS(watchdog0));

/// Channel returned when the timeout was installed.
int s_channel_id = -1;

} // namespace

bool initialize(uint32_t timeout_ms)
{
    if (!device_is_ready(s_p_watchdog_device))
    {
        LOG_ERR("watchdog device not ready");
        return false;
    }

    struct wdt_timeout_cfg timeout_config = {};
    timeout_config.flags = WDT_FLAG_RESET_SOC;
    timeout_config.window.min = 0U;
    timeout_config.window.max = timeout_ms;

    s_channel_id = wdt_install_timeout(s_p_watchdog_device, &timeout_config);

    if (s_channel_id < 0)
    {
        LOG_ERR("wdt_install_timeout failed (%d)", s_channel_id);
        return false;
    }

    const int result = wdt_setup(s_p_watchdog_device, WDT_OPT_PAUSE_HALTED_BY_DBG);

    if (result != 0)
    {
        LOG_ERR("wdt_setup failed (%d)", result);
        return false;
    }

    LOG_INF("watchdog started with a %u ms timeout", timeout_ms);
    return true;
}

void feed()
{
    if (s_channel_id >= 0)
    {
        (void)wdt_feed(s_p_watchdog_device, s_channel_id);
    }
}

} // namespace hal::watchdog

/// @}
