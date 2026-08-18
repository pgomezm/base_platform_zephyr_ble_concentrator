/// @addtogroup grp_svc_system_diagnostics
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the system diagnostics service.

#include "svc/system_diagnostics/subsystem.hpp"

#include "app/port_list.hpp"
#include "eda/active_object/active_object.hpp"
#include "eda/timer/timer.hpp"
#include "hal/led/led.hpp"
#include "hal/watchdog/watchdog.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(svc_system_diagnostics, CONFIG_APP_LOG_LEVEL);

namespace svc::system_diagnostics
{
namespace
{

/// Stack for the diagnostics thread.
K_THREAD_STACK_DEFINE(s_stack, eda::ActiveObject::s_stack_size);

/// Period of the heartbeat, in milliseconds.
constexpr uint32_t k_heartbeat_period_ms = 1000U;

/// Watchdog timeout, in milliseconds.
///
/// Several heartbeat periods, so a single late tick under load does not reset a
/// healthy device.
constexpr uint32_t k_watchdog_timeout_ms = 10U * k_heartbeat_period_ms;

/// How often the health summary is logged, in heartbeats.
constexpr uint32_t k_health_log_interval = 60U;

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port{s_active_object};

/// Fires every heartbeat period.
eda::Timer s_heartbeat_timer{s_port, static_cast<uint32_t>(Event::HEARTBEAT_DUE)};

/// Heartbeats since boot.
uint32_t s_heartbeat_count = 0U;

/// Log the counters that say whether the concentrator is keeping up.
void log_health()
{
    LOG_INF("health: %u devices tracked, %u reports dropped, %u devices evicted",
            device_table::get_device_count(), acquisition::get_dropped_report_count(),
            device_table::get_evicted_count());
}

} // namespace

bool initialize()
{
    s_active_object.init_task(app::TaskPriorities::SYSTEM_DIAGNOSTICS, "diagnostics", s_stack,
                              eda::ActiveObject::s_stack_size);

    if (!hal::watchdog::initialize(k_watchdog_timeout_ms))
    {
        LOG_WRN("watchdog unavailable, continuing without it");
    }

    s_heartbeat_timer.start_periodic(k_heartbeat_period_ms);

    LOG_INF("system diagnostics ready");

    return true;
}

Port& get_port()
{
    return s_port;
}

void Port::handle_event(uint32_t event_id, uint32_t opt_data)
{
    ARG_UNUSED(opt_data);

    switch (static_cast<Event>(event_id))
    {
    case Event::HEARTBEAT_DUE:
        // Fed from here and nowhere else: a watchdog fed by the thread that
        // needs watching proves only that one thread is alive.
        hal::watchdog::feed();
        hal::led::toggle(hal::led::Id::HEARTBEAT);

        ++s_heartbeat_count;

        if ((s_heartbeat_count % k_health_log_interval) == 0U)
        {
            log_health();
        }
        break;

    case Event::INVALID:
    default:
        LOG_WRN("unhandled event id %u", event_id);
        break;
    }
}

} // namespace svc::system_diagnostics

/// @}
