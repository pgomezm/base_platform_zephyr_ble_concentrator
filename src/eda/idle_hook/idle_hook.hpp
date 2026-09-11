/// @addtogroup grp_eda
/// @{
///
/// @file idle_hook.hpp
///
/// Idle hook header file.
/// Feeds the watchdog from idle, and runs one optional application callback
/// there.

#pragma once

namespace eda
{

/// @brief What runs when the system has nothing else to run
///
/// Wired to the backend's idle-time mechanism by the application, once, at
/// bring-up: `hal::os::register_idle_callback(&eda::IdleHook::invoke)`. From
/// then on invoke() runs whenever every other thread is blocked or sleeping.
///
/// **It feeds the watchdog.** Idle is the only context where feeding proves
/// anything: a watchdog fed by a thread proves that one thread is alive, while
/// a watchdog fed from idle proves the system as a whole still gets there.
/// Anything spinning or deadlocked above idle stops the feeds and the device
/// resets. The feed is not the registered callback and cannot be replaced by
/// one — it happens first, and it happens whether anything is registered or
/// not.
///
/// On top of that, one callback may be registered, for background work that
/// only makes sense when nothing else is running. Idle has no schedule, so
/// whatever goes there runs at a rate nobody chose: work that has to happen at
/// a known rate belongs to an active object, where it has a priority.
///
/// This class holds no RTOS type. It reaches the watchdog through
/// `hal::watchdog`, by interface, with the platform on the far side of the
/// seam. Where invoke() is called from is `hal::os`'s problem, not this
/// class's.
///
/// @note Only one callback can be registered at a time.
/// @note The callback must be non-blocking and must not call anything that
///       could block the thread invoke() runs on.
class IdleHook
{
public:
    /// @brief Callback function type for idle hook
    using IdleCallback = void (*)();

    /// @brief Register a callback to be executed during idle time
    ///
    /// @param callback Function pointer to the callback. Pass nullptr to unregister.
    static void register_callback(IdleCallback callback);

    /// @brief Feed the watchdog, then invoke the registered callback
    ///
    /// Called from hal::os's idle-time mechanism (see hal/os/os.hpp), which
    /// means very often. If no callback is registered, only the feed happens.
    static void invoke();

private:
    static IdleCallback m_callback;
};

} // namespace eda

/// @}
