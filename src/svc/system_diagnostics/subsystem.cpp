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
#include "utils/fault/fault.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_DEFINE(svc_system_diagnostics);

namespace svc::system_diagnostics
{

// Every event id addressed to this port has to be a valid index into the port's
// callback table, or execute_callback() drops it without a word.
static_assert(static_cast<uint32_t>(Event::HEARTBEAT_DUE)
                  < static_cast<uint32_t>(MAX_PORT_CALLBACKS),
              "svc::system_diagnostics has more events than MAX_PORT_CALLBACKS allows");

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

/// True once the fault has been announced, so the first log happens once.
bool s_fault_announced = false;

/// Phase of the fault blink. Both LEDs follow it, so they stay in step.
bool s_fault_blink_on = false;

/// The active object that runs this service.
eda::ActiveObject s_active_object;

/// The port of this service.
Port s_port;

/// eda::Timer callback: posts HEARTBEAT_DUE to this service's own port.
///
/// Runs in the hal::os timer-service context, which may be an interrupt: hands
/// off and
/// returns, per docs/ARCHITECTURE.md section 4.
///
/// @param p_timer unused, there is only ever one heartbeat timer
void on_heartbeat_timer_expired(eda::Timer* p_timer)
{
    (void)p_timer;

    eda::Port::send_event_from_isr(app::PortList::SYSTEM_DIAGNOSTICS_PORT,
                                   static_cast<uint32_t>(Event::HEARTBEAT_DUE),
                                   0);
}

/// Fires every heartbeat period.
eda::Timer s_heartbeat_timer{"heartbeat", HEARTBEAT_PERIOD_MS, true, &on_heartbeat_timer_expired};

/// Heartbeats since boot.
uint32_t s_heartbeat_count = 0U;

/// Log the counters that say whether the concentrator is keeping up.
void log_health()
{
    LOG_INFO("health: %u devices tracked, %u reports dropped, %u devices evicted",
             device_table::get_device_count(),
             acquisition::get_dropped_report_count(),
             device_table::get_evicted_count());
}

/// Show the fault: ERROR and ACTIVITY blinking together, heartbeat off.
///
/// The watchdog keeps being fed, so this blinks until somebody looks at it. A
/// device that resets itself erases what went wrong.
///
/// On a board with no LEDs there is nothing to see and the log is all there is,
/// which is why the reason is repeated rather than said once.
void annunciate_fault()
{
    auto& leds = hal::led::Manager::get_instance();

    if (!s_fault_announced)
    {
        s_fault_announced = true;
        (void)leds.get_led(hal::led::LedInstances::HEARTBEAT_LED).turn_off();
        LOG_ERROR("fault: %s", utils::fault::describe(utils::fault::get_reason()));
    }
    else if ((s_heartbeat_count % HEALTH_LOG_INTERVAL) == 0U)
    {
        LOG_ERROR("fault: %s", utils::fault::describe(utils::fault::get_reason()));
    }
    else
    {
        // Nothing to say this tick.
    }

    s_fault_blink_on = !s_fault_blink_on;

    auto& error_led = leds.get_led(hal::led::LedInstances::ERROR_LED);
    auto& activity_led = leds.get_led(hal::led::LedInstances::ACTIVITY_LED);

    if (s_fault_blink_on)
    {
        (void)error_led.turn_on();
        (void)activity_led.turn_on();
    }
    else
    {
        (void)error_led.turn_off();
        (void)activity_led.turn_off();
    }
}

} // namespace

bool initialize()
{
    s_active_object.init_task(app::TaskPriorities::SYSTEM_DIAGNOSTICS, "diagnostics");
    s_port.init(app::PortList::SYSTEM_DIAGNOSTICS_PORT, s_active_object);

    if (hal::watchdog::WatchdogFactory::get_instance().set_timeout(WATCHDOG_TIMEOUT_MS)
        != hal::watchdog::WatchdogError::NO_ERROR)
    {
        LOG_WARNING("watchdog unavailable, continuing without it");
    }

    s_heartbeat_timer.start();

    LOG_INFO("system diagnostics ready");

    return true;
}

Port& get_port()
{
    return s_port;
}

void Port::execute_event(uint32_t event_id, uint32_t opt_data_address)
{
    switch (static_cast<Event>(event_id))
    {
    case Event::HEARTBEAT_DUE:
        // Fed from here and nowhere else: a watchdog fed by the thread that
        // needs watching proves only that one thread is alive.
        (void)hal::watchdog::WatchdogFactory::get_instance().refresh();

        ++s_heartbeat_count;

        // A faulted device keeps being fed and keeps blinking. It stops
        // reporting that it is healthy, because it is not.
        if (utils::fault::is_active())
        {
            annunciate_fault();
            break;
        }

        (void)hal::led::Manager::get_instance()
            .get_led(hal::led::LedInstances::HEARTBEAT_LED)
            .toggle();

        if ((s_heartbeat_count % HEALTH_LOG_INTERVAL) == 0U)
        {
            log_health();
        }
        break;

    case Event::INVALID:
    default:
        LOG_WARNING("unhandled event id %u", event_id);
        break;
    }

    // The switch above is what this service does with the event. This is how
    // another module hears about it without this service knowing it exists.
    execute_callback(event_id, opt_data_address);
}

} // namespace svc::system_diagnostics

/// @}
