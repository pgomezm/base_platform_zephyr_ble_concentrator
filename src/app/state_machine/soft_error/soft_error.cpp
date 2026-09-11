/// @addtogroup grp_app
/// @{
///
/// @file soft_error.cpp
///
/// Source file that implements the soft error state.

#include "app/app.hpp"
#include "app/state_machine/soft_error/soft_error.hpp"
#include "app/state_machine/state_machine.hpp"
#include "app/app_config.hpp"
#include "app/port.hpp"
#include "eda_config/port_list.hpp"

#include "eda/port/port.hpp"
#include "eda/timer/timer.hpp"
#include "hal/led/led.hpp"
#include "svc/comms/port.hpp"
#include "utils/log/log.hpp"

LOG_MODULE_USE(app);

namespace app
{
namespace
{

/// How long to wait before the next join attempt, in milliseconds.
///
/// The backoff itself, not a counter it is derived from. Doubles on every
/// failure, stops at the cap, and only a successful join puts it back. It
/// survives leaving and re-entering the state on purpose: a device that has
/// been unable to reach its server for an hour should not go back to trying
/// every 30 s because a dispatch happened to fail.
uint32_t s_retry_delay_ms = SOFT_ERROR_RETRY_MS;

/// Posts RETRY_TIMEOUT to the application when the backoff delay elapses.
///
/// Runs in the timer-service context, which may be an interrupt: hands off and
/// returns, per docs/ARCHITECTURE.md section 4.
///
/// @param p_timer unused, there is only ever one retry timer
void on_retry_timer_expired(eda::Timer* p_timer)
{
    (void)p_timer;

    eda::Port::send_event_from_isr(eda_config::PortList::APP_PORT,
                                   static_cast<uint32_t>(Event::RETRY_TIMEOUT),
                                   0);
}

/// One-shot: every arm sets its own delay, so the period given here is only a
/// placeholder.
eda::Timer s_retry_timer{"soft-retry", SOFT_ERROR_RETRY_MS, false, &on_retry_timer_expired};

/// Wait, then try to join again.
void schedule_retry()
{
    LOG_WARNING("join failed, retrying in %u s", static_cast<unsigned>(s_retry_delay_ms / 1000U));

    (void)s_retry_timer.start(s_retry_delay_ms);

    // Doubled after arming, so this wait is the one that was announced and the
    // next failure is the one that pays for it. Compared against half the cap
    // rather than doubled and then clamped, so the multiplication cannot
    // overflow on the way.
    if (s_retry_delay_ms > (SOFT_ERROR_RETRY_MAX_MS / 2U))
    {
        s_retry_delay_ms = SOFT_ERROR_RETRY_MAX_MS;
    }
    else
    {
        s_retry_delay_ms *= 2U;
    }
}

} // namespace

SoftErrorState::SoftErrorState(eda::StateMachine& state_machine)
    : eda::State("SOFT_ERROR", &state_machine)
{}

void SoftErrorState::entry()
{
    // Solid, where the hard error state blinks two together. One glance tells
    // the difference: this device is still trying, that one has stopped.
    (void)hal::led::Manager::get_instance().get_led(hal::led::LedInstances::ERROR_LED).turn_on();

    // Scanning is deliberately left running. The table keeps filling while the
    // uplink is down, and whatever is too old to be worth sending is dropped at
    // the next dispatch by DEVICE_STALE_AFTER_S rather than here.
    schedule_retry();
}

void SoftErrorState::exit()
{
    // Before the LED, and unconditionally. A timer left armed on the way out
    // fires into whichever state comes next, and the only reason that is
    // harmless today is that the other states ignore the event.
    s_retry_timer.stop();

    (void)hal::led::Manager::get_instance().get_led(hal::led::LedInstances::ERROR_LED).turn_off();
}

void SoftErrorState::dispatch_event(uint32_t event_id, uint32_t opt_data_address)
{
    (void)opt_data_address;

    switch (static_cast<Event>(event_id))
    {
    case Event::RETRY_TIMEOUT:
        eda::Port::send_event(eda_config::PortList::COMMS_PORT,
                                       static_cast<uint32_t>(svc::comms::Event::JOIN_NETWORK),
                                       0);
        break;

    case Event::NETWORK_JOIN_FAILED:
        // Stay here and wait longer. Leaving for another state and coming back
        // would work too, but it would run entry() again and re-arm from the
        // counter twice for one failure.
        schedule_retry();
        break;

    case Event::NETWORK_JOINED:
        s_retry_delay_ms = SOFT_ERROR_RETRY_MS;
        App::get_instance().get_state_machine().transition_to(StateId::LISTENING);
        break;

    case Event::HARD_ERROR:
        // Not reached by a join failure any more: no number of unreachable
        // servers makes a working device unrecoverable. This is still here for
        // whatever else decides the device has to stop.
        App::get_instance().get_state_machine().transition_to(StateId::HARD_ERROR);
        break;

    default:
        break;
    }
}

} // namespace app

/// @}
