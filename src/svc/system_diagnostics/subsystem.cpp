/// @addtogroup grp_svc_system_diagnostics
/// @{
///
/// @file subsystem.cpp
///
/// Source file that implements the system diagnostics service.

#include "svc/system_diagnostics/subsystem.hpp"

#include "app/port_list.hpp"
#include "app/tasks_priorities.hpp"
#include "eda/active_object/active_object.hpp"
#include "eda/port/port.hpp"
#include "eda/timer/timer.hpp"
#include "hal/led/led.hpp"
#include "hal/watchdog/watchdog.hpp"
#include "svc/acquisition/subsystem.hpp"
#include "svc/device_table/subsystem.hpp"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(svc_system_diagnostics, CONFIG_APP_LOG_LEVEL);

namespace svc::system_diagnostics
{
namespace
{

/// Period of the heartbeat, in milliseconds.
constexpr uint32_t HEARTBEAT_PERIOD_MS = 1000U;

/// Watchdog timeout, in milliseconds.
///
/// Several heartbeat periods, so a single late tick under load does not reset a
/// healthy device.
constexpr uint32_t WATCHDOG_TIMEOUT_MS = 10U * HEARTBEAT_PERIOD_MS;

/// How often the health summary is logged, in heartbeats.
constexpr uint32_t HEALTH_LOG_INTERVAL = 60U;

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port;

/// eda::Timer callback: posts HEARTBEAT_DUE to this service's own port.
///
/// Runs in the hal::os timer-service context (a Zephyr ISR): hands off and
/// returns, per docs/ARCHITECTURE.md section 4.
///
/// @param p_timer unused, there is only ever one heartbeat timer
void on_heartbeat_timer_expired(eda::Timer* p_timer)
{
    ARG_UNUSED(p_timer);

    eda::Port::send_event_from_isr(app::PortList::SYSTEM_DIAGNOSTICS_PORT,
                                   static_cast<uint32_t>(Event::HEARTBEAT_DUE), 0);
}

/// Fires every heartbeat period.
eda::Timer s_heartbeat_timer{"heartbeat", HEARTBEAT_PERIOD_MS, true, &on_heartbeat_timer_expired};

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
    s_active_object.init_task(app::TaskPriorities::SYSTEM_DIAGNOSTICS, "diagnostics");
    s_port.init(app::PortList::SYSTEM_DIAGNOSTICS_PORT, s_active_object);

    if (hal::watchdog::WatchdogFactory::get_instance().set_timeout(WATCHDOG_TIMEOUT_MS) !=
        hal::watchdog::WatchdogError::NO_ERROR)
    {
        LOG_WRN("watchdog unavailable, continuing without it");
    }

    s_heartbeat_timer.start();

    LOG_INF("system diagnostics ready");

    return true;
}

Port& get_port()
{
    return s_port;
}

void Port::execute_event(uint32_t event_id, uint32_t opt_data_address)
{
    ARG_UNUSED(opt_data_address);

    switch (static_cast<Event>(event_id))
    {
    case Event::HEARTBEAT_DUE:
        // Fed from here and nowhere else: a watchdog fed by the thread that
        // needs watching proves only that one thread is alive.
        (void)hal::watchdog::WatchdogFactory::get_instance().refresh();
        (void)hal::led::Manager::get_instance().get_led(hal::led::LedInstances::HEARTBEAT_LED).toggle();

        ++s_heartbeat_count;

        if ((s_heartbeat_count % HEALTH_LOG_INTERVAL) == 0U)
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
