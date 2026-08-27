/// @addtogroup grp_eda
/// @{
///
/// @file idle_hook.hpp
///
/// Idle hook header file.
/// Provides infrastructure for registering callbacks to be executed during idle time.

#pragma once

#include <cstdint>

namespace eda
{

/// @brief Idle hook manager for idle-time callbacks
///
/// This class provides a mechanism for the application to register a callback
/// that will be executed while the backend has nothing else to run. This is
/// useful for low-priority background work that should only happen when the
/// system is idle.
///
/// The class holds no RTOS type at all. What differs between backends is only
/// where invoke() gets called from, which is hal/os/os.hpp's
/// register_idle_callback() problem and not this class's.
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

    /// @brief Invoke the registered callback
    ///
    /// Called from hal::os's idle-time mechanism (see hal/os/os.hpp). If no
    /// callback is registered, this function does nothing.
    static void invoke();

private:
    static IdleCallback m_callback;
};

} // namespace eda

/// @}
