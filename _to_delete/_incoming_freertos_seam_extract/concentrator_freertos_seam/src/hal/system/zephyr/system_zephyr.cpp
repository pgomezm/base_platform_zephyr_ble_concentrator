/// @addtogroup grp_hal_system
/// @{
///
/// @file system_zephyr.cpp
///
/// Source file that implements the system HAL on Zephyr.

#include "hal/system/system.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>

#if defined(CONFIG_HWINFO)
#include <zephyr/drivers/hwinfo.h>
#endif

namespace hal::system
{

ResetReason get_reset_reason()
{
#if defined(CONFIG_HWINFO)
    uint32_t cause = 0U;

    if (hwinfo_get_reset_cause(&cause) != 0)
    {
        return ResetReason::UNKNOWN;
    }

    // Checked most-specific first: a watchdog or brownout reset is the
    // interesting one, and some causes are reported together with POR.
    if ((cause & RESET_WATCHDOG) != 0U)
    {
        return ResetReason::WATCHDOG;
    }

    if ((cause & RESET_BROWNOUT) != 0U)
    {
        return ResetReason::BROWNOUT;
    }

    if ((cause & RESET_SOFTWARE) != 0U)
    {
        return ResetReason::SOFTWARE;
    }

    if ((cause & RESET_PIN) != 0U)
    {
        return ResetReason::PIN_RESET;
    }

    if ((cause & RESET_POR) != 0U)
    {
        return ResetReason::POWER_ON;
    }

    return ResetReason::UNKNOWN;
#else
    return ResetReason::UNKNOWN;
#endif
}

uint32_t get_uptime_seconds()
{
    return static_cast<uint32_t>(k_uptime_get() / 1000);
}

void reset()
{
    sys_reboot(SYS_REBOOT_COLD);
}

} // namespace hal::system

/// @}
