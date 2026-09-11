/// @addtogroup grp_eda
/// @{
///
/// @file idle_hook.cpp
///
/// Idle hook implementation file.

#include "eda/idle_hook/idle_hook.hpp"

#include "hal/watchdog/watchdog.hpp"

namespace eda
{

// Initialize static member
IdleHook::IdleCallback IdleHook::m_callback = nullptr;

void IdleHook::register_callback(IdleCallback callback)
{
    m_callback = callback;
}

void IdleHook::invoke()
{
    // Every time, with no rate limiting and no state to keep. The backend's
    // idle mechanism is already a loop that would otherwise only yield, so what
    // this costs is cycles nothing else wanted. Feeding a watchdog is a single
    // register write behind hal::watchdog; anything a backend adds to that
    // belongs on the other side of the seam, not in a counter here.
    //
    // Before the callback, and not conditional on it: what the watchdog watches
    // is whether the system reaches idle at all, and that is already true by
    // the time this line runs.
    (void)hal::watchdog::WatchdogFactory::get_instance().refresh();

    if (m_callback != nullptr)
    {
        m_callback();
    }
}

} // namespace eda

/// @}
