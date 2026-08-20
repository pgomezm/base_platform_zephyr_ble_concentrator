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

class Watchdog : public IWatchdog
{
public:
    Watchdog() : m_channel_id(-1), m_timeout_ms(0U) {}

    WatchdogError set_timeout(uint32_t timeout_ms) override
    {
        // Zephyr installs a watchdog timeout once and does not allow it to be
        // changed afterwards, which is why this reports ALREADY_RUNNING rather
        // than silently ignoring the second call.
        if (m_channel_id >= 0)
        {
            LOG_WRN("watchdog already running with a %u ms timeout", m_timeout_ms);
            return WatchdogError::ALREADY_RUNNING;
        }

        if ((timeout_ms < MIN_TIMEOUT_MS) || (timeout_ms > MAX_TIMEOUT_MS))
        {
            LOG_ERR("timeout %u ms is outside [%u, %u]", timeout_ms, MIN_TIMEOUT_MS,
                    MAX_TIMEOUT_MS);
            return WatchdogError::INVALID_TIMEOUT;
        }

        if (!device_is_ready(s_p_watchdog_device))
        {
            LOG_ERR("watchdog device not ready");
            return WatchdogError::HARDWARE_ERROR;
        }

        struct wdt_timeout_cfg timeout_config = {};
        timeout_config.flags = WDT_FLAG_RESET_SOC;
        timeout_config.window.min = 0U;
        timeout_config.window.max = timeout_ms;

        const int channel_id = wdt_install_timeout(s_p_watchdog_device, &timeout_config);

        if (channel_id < 0)
        {
            LOG_ERR("wdt_install_timeout failed (%d)", channel_id);
            return WatchdogError::HARDWARE_ERROR;
        }

        const int result = wdt_setup(s_p_watchdog_device, WDT_OPT_PAUSE_HALTED_BY_DBG);

        if (result != 0)
        {
            LOG_ERR("wdt_setup failed (%d)", result);
            return WatchdogError::HARDWARE_ERROR;
        }

        m_channel_id = channel_id;
        m_timeout_ms = timeout_ms;

        LOG_INF("watchdog started with a %u ms timeout", timeout_ms);

        return WatchdogError::NO_ERROR;
    }

    WatchdogError refresh() override
    {
        if (m_channel_id < 0)
        {
            return WatchdogError::HARDWARE_ERROR;
        }

        if (wdt_feed(s_p_watchdog_device, m_channel_id) != 0)
        {
            return WatchdogError::HARDWARE_ERROR;
        }

        return WatchdogError::NO_ERROR;
    }

    uint32_t get_timeout() const override
    {
        return m_timeout_ms;
    }

private:
    int m_channel_id;
    uint32_t m_timeout_ms;
};

} // namespace

IWatchdog& WatchdogFactory::get_instance()
{
    static Watchdog instance;

    return instance;
}

} // namespace hal::watchdog

/// @}
